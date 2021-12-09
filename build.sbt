lazy val root = project
  .in(file("."))
  .settings(
    name := "HippoCafe",
    description := "A Java Class File library written in Scala 3.",
    version := "2.2.7",
    scalaVersion := "3.1.0",

    libraryDependencies += "org.scalatest" %% "scalatest" % "3.2.9" % Test
  )