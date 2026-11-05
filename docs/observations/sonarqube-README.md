# SonarQube analysis

Project: `1298345095_wxnotepad2`, default branch `main`.

The 2026-11-04 MCP baseline at application revision
`7a3da91cadaaf93accab530cacd178a76aecc494` contained 863 open issues.
Of these, 48 affected security or reliability. The quality gate reported new-code
security and reliability ratings of E and 5.3% duplicated lines (limit: 3%).
Raw MCP responses and local validation logs are retained outside the checkout in
`E:/NewNotepad4/wxnotepad-evidence/sonarqube-2026-11-04/`.

## Code changes

- Assign the Find All anchor count directly to its `std::optional` and make the
  presence check explicit. The existing initialization and original anchor-count
  behavior are preserved.
- Put file fixtures beneath a shared, uniquely named private test root. Directory
  creation rejects existing paths; Unix permissions are 0700. Existing canonical
  Windows paths and per-test cleanup remain supported.
- Reuse the existing filename conversion and saved-byte readers across fixtures,
  removing repeated helper implementations while retaining independent test cases.
- Give the private Wayland driver a fixed geometry response inside its own runtime
  directory. The fixture requests geometry without supplying a write destination.
- Use explicit measurement checks that remain active with Python optimization.
  Preserve failed-row recording and the independent, full saved-byte comparison.
- Require HTTPS for both the actionlint download and redirects.

## Analysis scope

Automatic analysis reads `.sonarcloud.properties`: application sources, tests,
developer tools and workflows all remain in `sonar.sources`. CFamily disregards
`sonar.tests`, so C++ fixtures must stay in source scope to retain analysis. The three generated language
scheme tables are excluded from copy/paste metrics because their contents and
ordering come from the pinned upstream schemes. Other code analysis remains
enabled for those tables. Quality-gate thresholds and rule profiles are unchanged.

Reference: [SonarQube Cloud automatic analysis configuration](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/automatic-analysis).
See also [CFamily test-file analysis](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/languages/c-family/customizing-the-analysis).

## CLI finding review

The command runners intentionally accept a local executable, repository, build
directory or output directory from the developer. Their subprocess calls use
argument arrays with shell execution disabled. Formatter inputs come from tracked
`src/` and `tests/` paths; analyzer inputs are resolved and restricted to those
directories. Generator revisions and Git object paths are pinned or come from the
pinned Git tree. The resource report destination is the explicit CLI output
directory, created before reading app state; observed state is serialized as data.

These data flows must be considered when reviewing injection findings. Preserve
the documented CLI contracts when resolving a false positive, and attach the
specific source-to-sink explanation to each reviewed finding.
