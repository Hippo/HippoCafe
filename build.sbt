/*name := "HippoCafe"

version := "1.5.1"

scalaVersion := "3.0.1"

resolvers += "jitpack" at "https://jitpack.io"

libraryDependencies += "org.scalatest" %% "scalatest" % "3.0.8" % Test*/

lazy val root = project
  .in(file("."))
  .settings(
    name := "HippoCafe",
    description := "A Java Class File library written in Scala 3.",
    version := "2.0.0",
    scalaVersion := "3.0.1",

    libraryDependencies += "org.scalatest" %% "scalatest" % "3.2.9" % Test
  )