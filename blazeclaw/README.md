Developer notes

This workspace uses vcpkg for dependency management in CI. To install dependencies locally run:

> .\vcpkg\vcpkg install nlohmann-json catch2

The test project BlazeClawMfc.Tests expects Catch2 and nlohmann::json available via vcpkg.

CI notes:
 azure-pipelines.yml added to run vcpkg install, build the solution with msbuild, and run the test executable.

Local setup example (PowerShell):
  cd E:\gitRepo\blazeClaw
  .\vcpkg\vcpkg.exe install nlohmann-json catch2
  msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64
