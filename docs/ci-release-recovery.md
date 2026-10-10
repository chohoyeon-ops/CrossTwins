# CrossTwins CI / Release 복구 절차

이 문서는 `.github/workflows/release.yml`의 현재 계약을 설명한다. 펌웨어 기능이나 SDK revision을 바꾸는 복구 절차가 아니다.

## SDK dependency 정책

- Canonical SDK: `https://github.com/chohoyeon-ops/freeink-sdk.git` (공개 GitHub fork).
- Upstream: `https://github.com/Free-Ink/freeink-sdk.git`. 로컬 SDK remote는 canonical fork를 `origin`, upstream을 `upstream`으로 구분한다.
- Exact gitlink: `539da34db67a546cad961525d7e9b6364dfa2fe4`. SDK source를 재작성하지 않고 원래 SHA와 history를 게시했다.
- Fork의 `main`은 upstream lineage를 유지한다. `cross-twins`는 CrossTwins 전용 SDK commit과 필요한 조상을 보존하는 ref이며, firmware dependency의 기준은 branch tip이 아니라 superproject gitlink이다.
- 이 revision은 parent `111fdcc7f0176c3ee38391a160ee296bf492dbd8` 위의 stream-transfer backport이다. SD 읽기 오류와 premature EOF, stalled write를 실패로 반환하고 positive short write를 재시도한다. 다른 upstream SDK 기능을 함께 가져오지 않기 위해 기존 pin을 유지한다.
- 로컬 개발자와 CI는 같은 공개 URL에서 같은 gitlink를 recursive checkout한다. private credential, 로컬 filesystem dependency 또는 CI 전용 checkout workaround를 요구하지 않는다.
- `branch = main` hint는 `git submodule update --remote`용이며 exact checkout의 재현성에 필요하지 않아 제거했다. hint가 없어도 `--remote`는 원격 기본 branch를 사용할 수 있으므로 자동 SDK 업데이트에 사용하지 않는다.
- SDK 업데이트는 별도 승인된 변경으로 source diff와 firmware 검증을 거친다. 새 commit을 원격의 유지되는 ref에 먼저 게시한 뒤 superproject gitlink를 명시적으로 갱신한다. branch sync만으로 firmware pin을 바꾸지 않는다.
- `not our ref <SHA>`가 재발하면 exact commit의 원격 접근 가능성을 확인한다. SDK를 최신 `main`으로 바꾸거나 submodule checkout을 제거하는 방식으로 우회하지 않는다.

## 2026-10-09 SDK 정상화 검증

- 공개 fork 관계(`Free-Ink/freeink-sdk` parent)와 `cross-twins`의 exact SHA를 확인했다. fork의 `main`은 변경하지 않았다.
- 인증 없는 독립 SDK clone에서 exact HEAD, parent, 전체 history 594개 commit, `fsck --full` 및 clean working tree를 검증했다.
- 별도의 공개 CrossTwins clone에 미게시 `.gitmodules` 변경만 적용한 사전 검증에서 normal recursive submodule 초기화가 성공했다. SDK는 exact gitlink와 일치하고, 내부 lucide submodule도 고정된 `c81680e066f45b640743ca78ae36cdedda3f0318`로 checkout됐다.
- 이 새 dependency checkout에서 `gh_release`와 `gh_release_rc` 빌드가 모두 exit 0으로 성공했다. release는 4,269,296 bytes, RC는 4,269,344 bytes이다. ESP32-C3 header/checksum/validation hash와 compile-time version(`1.6.5`, `1.6.5-rc+f7111e8`)을 확인했다. 빌드 source 기준은 `f7111e805f422d5146a782f9fdf575600e8b62a5`에 미게시 dependency descriptor만 적용한 상태다.
- 프로젝트 source/object/cache는 새 디렉터리를 사용했다. global PlatformIO toolchain과 tuned Arduino framework cache는 재사용했으므로 ESP-IDF descriptor의 과거 version/date를 현재 application version으로 간주하지 않는다. outer/penv Core는 모두 6.1.19로 확인했다.
- firmware와 SHA-256 metadata는 고유한 `CrossTwins-SDK-CI-Recovery-20261009-223637-f7111e8` 폴더로 로컬 `C:\Codex\korean-artifacts`와 iCloud `CrossTwins\DeviceValidation`에 보관했고 파일 hash 일치를 검증했다.
- 게시된 `192e7e2c568f7ecd0b6276e3c9a4cf60a23a54e9`의 인증 없는 fresh recursive clone이 수동 변경 없이 성공했다. superproject와 SDK working tree가 clean이고 gitlink와 SDK HEAD가 모두 `539da34db67a546cad961525d7e9b6364dfa2fe4`이다.
- 실제 [CI run 37939616105](https://github.com/chohoyeon-ops/CrossTwins/actions/runs/37939616105)의 모든 recursive checkout이 성공했다. 호스트 테스트 554개와 firmware 빌드 5개(default, sticky, x4pro, x4c, papermono)도 모두 통과했다. 전체 run은 아래 검사 실패 때문에 `completed / failure`로 종료됐다. SDK checkout 복구와 전체 CI 성공은 별개로 판정한다.
- 전체 CI의 별도 차단 원인은 기존 header 5개의 clang-format 차이와 cppcheck low/style 3건이다. `LibraryListActivity.cpp:42` 및 `TxtReaderActivity.cpp:180`의 `constParameterReference`, `TxtReaderActivity.cpp:113`의 `useStlAlgorithm`이 실패 로그에 기록됐다. firmware 리팩터링 금지 범위를 존중하여 코드를 고치거나 검사 정책을 완화하지 않았다. 최종 job별 결과와 로그는 이번 한국어 완료 보고서에 기록한다. 이 결과 반영은 로컬 문서 변경이며 추가 commit/push는 하지 않았다.

## 2026-10-10 품질 검사 복구 검증 — 전체 host 검증 대기

- 기준 `master / 192e7e2c568f7ecd0b6276e3c9a4cf60a23a54e9`와 원격 HEAD가 일치함을 재확인했다. 기존 `AGENTS.md`, `analysis/` 및 위의 미커밋 SDK 검증 기록을 보존했다.
- clang-format 21.1.8 및 기존 wrapper로 이전 실패의 header 5개를 재현·수정했다. 전체 wrapper 재실행에서 추가 diff가 없고, include 목록과 공백을 제외한 본문이 보존됐다.
- `drawRecentProgress`와 private `updateViewport`의 renderer 인자를 const reference로 바꿨다. 호출하는 renderer 메서드는 모두 const이며 override/interface 변경은 없다. `updateViewport`의 header/definition도 일치한다.
- QR 텍스트 예약 크기는 `size_t` 초기값을 가진 `std::accumulate`로 계산한다. 빈 컨테이너는 0, 비어 있지 않으면 줄바꿈 수 `size() - 1`에서 시작해 기존 순서대로 문자열 길이를 더한다. 부수효과나 early exit가 없고 unsigned overflow 규칙도 기존과 같다.
- 기존 `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`가 `No defects found`로 통과했다. suppression/threshold/workflow/toolchain/SDK 정책은 변경하지 않았다. 로컬 Windows cppcheck binary는 2.20.0(package 2.20.1), Core는 6.1.19이다. Linux CI와 OS가 다름을 구분한다.
- 관련 focused tests는 91/91 PASS다. 전체 host build는 기존 `ChapterHtmlSlimParserTest.cpp:121,126`의 Windows `wchar_t*` 경로와 `HalFile::open(const char*)` 불일치 때문에 실패했다. 지정된 수정 대상 밖의 테스트 두 호출을 `path.string().c_str()`로 바꾸는 추가 승인을 요청했으며 아직 적용하지 않았다. 테스트를 생략하거나 전체 PASS로 표현하지 않는다.
- `gh_release`와 `gh_release_rc`는 별도 object/cache root에서 exit 0으로 성공했다. 크기는 4,267,840 / 4,267,888 bytes이며, compile-time version은 `1.6.5` / `1.6.5-rc+192e7e2`이다. ESP32-C3 header/checksum/validation hash도 PASS다. global 설치 toolchain/framework는 재사용했다.
- firmware와 SHA-256/source patch metadata를 `CrossTwins-CI-Final-20261010-032604-192e7e2c` 이름으로 로컬 `C:\Codex\korean-artifacts`와 iCloud `CrossTwins\DeviceValidation`에 보존하고 read-back hash 일치를 확인했다. 미커밋 수정 빌드라는 provenance를 명시했다. iCloud 서버 동기화 완료는 별도 확인하지 못했다.
- 전체 로컬 host 검증과 새 GitHub CI는 아직 미완료다. 이번 변경은 commit/push하지 않았고 기존 run의 실패 상태를 새 결과로 대체하지 않았다. SDK gitlink와 `.gitmodules`는 그대로 유지했다. Release 작업은 수행하지 않았다.

### 추가 승인 후 Windows 전체 검증 결과

- 사용자가 승인한 `ChapterHtmlSlimParserTest.cpp`의 두 호출만 `path.string().c_str()`로 수정했다. 테스트 stub의 `HalFile::open`은 호출 중 `std::fopen`을 실행하고 `FILE*`만 보관하므로 임시 string의 포인터 수명이 안전하다. 현재 ASCII 임시 경로의 Windows narrow API 호환성을 확인했으며 일반 Unicode 경로 지원 변경은 하지 않았다.
- 전체 Windows host build는 성공했고 554개 실행 결과 553 PASS / 1 FAIL이다. 실패는 기존 `CssParserTest.FailedPromotionRestoresTheOnlyBackupCache`이며 단독 실행에서도 재현됐다. 전체 suite PASS로 표현하지 않는다.
- 원인: `CssParserTest.cpp:222`는 native path 문자열(`...\\css_rules.cache.tmp`)을 실패 주입 대상으로 등록하지만 parser는 `cachePath + "/css_rules.cache.tmp"`를 사용한다. `test/css_parser/stubs/HalStorage.h:71`의 raw string 비교가 false여서 의도한 rename 실패가 주입되지 않는다. standalone 진단에서 raw 문자열은 다르고 Windows filesystem path로는 같음을 확인했다. 그 결과 `writer.saveToCache(true)`가 예상 false 대신 true를 반환하고 replacement rule이 남는다.
- 새로운 CSS 테스트/stub 변경은 이번 두 줄 승인 밖이므로 적용하지 않았다. 검사와 threshold를 유지한 clang-format/cppcheck 재검증은 PASS이며, firmware source patch hash는 기존 release/RC 검증 당시와 동일하다. 전체 host 검증 실패에 따라 추가 build 재실행·commit·push·새 CI 실행은 보류했다. 기존 성공 firmware와 archive는 보존했다.
- 최소 대안은 실패 주입 경로를 parser와 같은 문자열로 등록하도록 해당 테스트 한 줄을 고치는 것이다. 예: `Storage.failNextRename(cachePath() + "/css_rules.cache.tmp", cachePath() + "/css_rules.cache");`. 아직 적용하지 않았다. source 범위를 넓히기 전 별도 승인이 필요하다.

### CSS 경로 수정 추가 승인 후 최종 로컬 검증

- 사용자 추가 승인에 따라 `CssParserTest.cpp:222` 실패 주입 경로 한 줄만 위의 최소 대안대로 일치시켰다. stub 및 parser 구현, 실패 주입과 모든 assertion은 유지했다.
- CSS 단독 테스트가 통과했다. `saveToCache(true)` 실패 반환, backup 복구, temporary/backup 정리, original rule만 남음 및 replacement rule 없음의 기존 검증이 모두 유지됐다. 전체 Windows host tests도 554/554 PASS이며 테스트 수는 바꾸지 않았다.
- 전체 clang-format wrapper 재실행에 추가 diff가 없고 `git diff --check` 및 기존 threshold의 cppcheck가 PASS다. `gh_release`/`gh_release_rc` 재빌드도 exit 0이며 ESP32-C3 header/checksum/validation hash가 유효하다. 최종 재검증은 이전 object cache를 재사용했다.
- 승인된 source/test 10개 파일과 이 CI 복구 문서만 commit/push 대상이다. SDK gitlink, `.gitmodules`, PlatformIO/dependency, 검사 정책, 사용자 `AGENTS.md`와 `analysis/`는 변경·포함하지 않는다. 전체 복구 완료 판정은 이후 새 push의 GitHub CI 최종 SUCCESS를 확인한 뒤 내려야 한다. 실제 run URL과 전체 job 결과는 별도 iCloud 최종 보고서에 기록한다. Release/tag 작업은 실행하지 않는다.

## Release 계약

- 실행 이벤트는 `release.published`이다. 태그 push만으로 실행되지 않으며, workflow가 Release를 생성하지도 않는다. 사용자가 승인한 Release 게시가 먼저 필요하다.
- checkout 대상은 Release tag이다. source의 `[crosspoint] version`을 태그에서 덮어쓰지 않고 비교한다.
- 태그의 leading 소문자 `v` 하나를 제거한다. 일반 Release는 source version과 같아야 하고, prerelease는 source version 뒤에 `rc`를 붙인 값이어야 한다.
- source version `1.6.5`의 경우 `1.6.5`와 `v1.6.5`는 일반 Release, `1.6.5rc`와 `v1.6.5rc`는 prerelease에서 유효하다. `v1.6.5-rc.1`은 현재 계약에서 유효하지 않다.
- X3와 X4는 별도 바이너리가 아니라 `gh_release` 또는 `gh_release_rc`의 공용 ESP32-C3 바이너리를 사용한다.
- X3/X4 asset은 일반 Release에서 `crosspoint-1.6.5-x3-x4.bin`, prerelease에서 `crosspoint-1.6.5rc-x3-x4.bin`이다.
- RC의 내부 firmware version은 `1.6.5-rc+<7자리 SHA>`이다. asset의 `1.6.5rc`와 표기는 다르지만 workflow 생성 및 upload 경로는 같은 규칙을 사용한다.
- Sticky, X4 Pro, X4 Classic, Paper Mono까지 총 5개 빌드가 모두 성공해야 upload job이 실행된다. `fail-fast: false`는 나머지 빌드를 계속할 뿐 부분 게시를 허용하지 않는다.
- upload는 `contents: write`를 갖는 job에서 `GH_TOKEN: ${{ github.token }}`으로 기존 Release에 5개 `.bin`을 첨부한다. `--clobber`는 같은 이름의 기존 asset을 교체한다.
- staged artifact 보존 기간은 1일이다. 오래된 실패 run은 성공한 job의 artifact도 다시 만들어야 할 수 있다.
- draft Release는 게시 전 실행되지 않는다. prerelease 선택은 Release 설정에서 결정된다. `GITHUB_TOKEN`으로 다른 workflow에서 생성한 Release 이벤트는 일반적으로 새 workflow를 유발하지 않으므로, 승인된 사용자 UI/CLI 게시 또는 별도 인증 설계를 사용한다.

## 빌드 환경과 기존 태그

현재 CI/Release는 바깥 core와 platform penv core를 모두 pioarduino `6.1.19`로 맞춘다. `custom_sdkconfig`의 Arduino framework 재빌드는 penv의 중첩 `pio`를 실행하므로 바깥 core만 고정해서는 충분하지 않다. core `6.2.0`의 SCons `4.11.1`과 platform `55.03.311`의 SCons `4.8.1` 충돌은 실제 실패 로그 및 [pioarduino issue #529](https://github.com/pioarduino/platform-espressif32/issues/529)에 기록되어 있다.

기존 `v1.6.5rc`의 source `3c3c8d8b`에는 Release penv 고정 수정이 없다. 현재 HEAD에는 `509a4fae`로 이미 반영되어 있다. 기존 run을 재실행해도 workflow와 tag source가 자동으로 현재 HEAD로 바뀌지 않는다. 기존 태그를 이동하지 말고, SDK 원격 복구와 수정이 포함된 source를 기준으로 사용자가 승인한 후속 tag/Release를 준비한다.

## 검증 순서

1. fresh checkout 및 SDK SHA 확인.
2. `actionlint`로 workflow 정적 검증.
3. `gh_release`와 prerelease가 필요하면 `gh_release_rc` 실제 빌드. 기존 artifact와 다른 build/archive 경로 사용.
4. 바이너리 크기, SHA-256, branch/HEAD/SDK revision과 build environment 기록.
5. 5개 보드 CI 성공 및 tag/version 검증 후, 명시적 승인에 따라 Release 게시와 asset upload 확인.

펌웨어 Release에는 현재 별도 사용자 secret이 필요하지 않다. 수동 font workflow의 `FONTS_REPO_TOKEN` 및 `crosspoint-reader/crosspoint-fonts`는 별도의 외부 게시 계약이며 firmware 실패의 원인으로 혼동하지 않는다.
