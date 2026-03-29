Developer notes

This workspace uses vcpkg for dependency management in CI. To install dependencies locally run:

  .\vcpkg\vcpkg.exe install nlohmann-json catch2

Test project and CI notes

- The test project BlazeClawMfc.Tests uses Catch2 (v3) and nlohmann::json via vcpkg.
- An Azure Pipelines definition (azure-pipelines.yml) has been added to install vcpkg packages, build the solution, and run the test executable.

Local setup example (PowerShell):

  cd E:\gitRepo\blazeClaw
  .\vcpkg\vcpkg.exe install --triplet x64-windows nlohmann-json catch2
  msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64

Notes:
- The project currently includes lightweight third_party stubs to ease local builds when vcpkg is not installed. It's recommended to use vcpkg as the source of truth and remove stubs once vcpkg is relied upon in CI.
- The tests are executed as part of CI in the pipeline; the pipeline runs the produced test executable and returns its exit code.
