#include "cafe/analysis/max_locals.hpp"

#include "analysis_detail.hpp"
#include "detail/member_signature.hpp"

namespace cafe::analysis {

std::expected<std::uint16_t, AnalysisError> compute_max_locals(
    const Method& method, const ClassFile& owner, const AnalysisLimits& limits) {
  const auto* code = method.code();
  if (!code)
    return method_site(owner, method).fail(AnalysisErrorKind::MissingCode, "max_locals requires decoded Code");
  return compute_max_locals(method, owner, code->code(), limits);
}

std::expected<std::uint16_t, AnalysisError> compute_max_locals(
    const Method& method, const ClassFile& owner, const MethodCode& code, const AnalysisLimits& limits) {
  const auto where = method_site(owner, method);
  if (!owns_method(owner, method))
    return where.fail(AnalysisErrorKind::InvalidCode, "method does not belong to the supplied class");
  if (where.class_name.empty())
    return where.fail(AnalysisErrorKind::InvalidConstantPoolReference, "invalid owning class name");
  // Apply the published verifier rules through Java SE 25.
  if (const auto version = owner.version(); version.major < 45 || version.major > 69 || !version.has_legal_minor())
    return where.fail(AnalysisErrorKind::UnsupportedClassVersion, "class version is outside the analysis profile");
  if (const auto* issue = detail::method_signature_issue(
          owner.version(), owner.access_flags(), method.access_flags(), where.name, where.descriptor))
    return where.fail(AnalysisErrorKind::InvalidDescriptor, issue);
  if (!detail::needs_code(method.access_flags(), where.name))
    return where.fail(AnalysisErrorKind::InvalidCode, "abstract and native methods have no Code limits");

  if (limits.cancellation.stop_requested())
    return where.fail(AnalysisErrorKind::Cancelled, "max_locals computation cancelled");
  if (code.size() > limits.max_instructions || code.size() > limits.max_work || code.size() > MAX_CODE_SIZE)
    return where.fail(AnalysisErrorKind::ResourceLimitExceeded, "instruction limit exceeded");
  unsigned required = *detail::method_parameter_slots(where.descriptor) +
                      (detail::is_static_method(method.access_flags(), where.name) ? 0u : 1u);
  const unsigned wire_limit = detail::short_code_header(owner.version()) ? UINT8_MAX : UINT16_MAX;
  if (required > wire_limit)
    return where.fail(AnalysisErrorKind::LocalOverflow, "method parameters cannot fit in the declared Code header");
  if (required > limits.max_local_slots)
    return where.fail(AnalysisErrorKind::ResourceLimitExceeded, "method parameters exceed the local slot limit");

  for (const auto& entry : code.instructions()) {
    if (limits.cancellation.stop_requested())
      return where.fail(AnalysisErrorKind::Cancelled, "max_locals computation cancelled", entry.id);
    const auto& info = opcode_info(entry.instruction.opcode());
    if (info.control_flow == ControlFlow::SubroutineCall || info.control_flow == ControlFlow::SubroutineReturn)
      return where.fail(AnalysisErrorKind::UnsupportedSubroutineAnalysis, "jsr and ret require legacy subroutine analysis",
                        entry.id);
    if (!info.is_allowed(owner.version().major) || info.form == OperandForm::WidePrefix ||
        info.logical_opcode != entry.instruction.opcode())
      return where.fail(AnalysisErrorKind::InvalidCode, "opcode is invalid for the declared class version", entry.id);
    if (info.local_width == 0)
      continue;
    auto local = entry.instruction.local_index();
    if (const auto increment = entry.instruction.increment_value())
      local = increment->local;
    if (!local)
      return where.fail(AnalysisErrorKind::InvalidCode, "missing local variable operand", entry.id);
    required = std::max(required, static_cast<unsigned>(*local) + info.local_width);
    if (required > wire_limit)
      return where.fail(AnalysisErrorKind::LocalOverflow, "local access cannot fit in max_locals", entry.id);
    if (required > limits.max_local_slots)
      return where.fail(AnalysisErrorKind::ResourceLimitExceeded, "local access exceeds the local slot limit", entry.id);
  }
  return static_cast<std::uint16_t>(required);
}

} // namespace cafe::analysis
