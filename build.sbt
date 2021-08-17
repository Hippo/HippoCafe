lazy val root = project
  .in(file("."))
  .settings(
    name := "HippoCafe",
    description := "A Java Class File library written in Scala 3.",
    version := "2.0.2",
    scalaVersion := "3.0.1",

    libraryDependencies += "org.scalatest" %% "scalatest" % "3.2.9" % Test
  )