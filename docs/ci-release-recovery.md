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
- 최종 무개입 anonymous recursive clone과 실제 GitHub Actions 결과는 CrossTwins 변경 게시 후 별도로 검증한다. SDK checkout 성공과 전체 CI 성공은 구분해 판정한다.

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
