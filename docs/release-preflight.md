# CrossTwins 사전 Release 검증 계약

## 실행 경계

`release.yml`은 `workflow_dispatch`만 지원하는 build-only workflow다.
Release event trigger, contents write permission, tag/Draft/Release/asset 쓰기
job은 없다. 이 workflow가 GitHub 기본 브랜치에 반영되기 전에는 원격에서
기존 published workflow가 여전히 유효하므로 Release를 게시하지 않는다.

CI 변경을 포함한 새 커밋의 full SHA로 dispatch한다. workflow 실행 ref도
그 SHA를 가리켜야 한다. `commit_sha == GITHUB_SHA == checkout HEAD`를 검사한다.
버전 이름에 사용하는 SHA는 full SHA의 첫 7자리다. 태그는 아직 없어도 된다.
`release_tag=v1.6.5-ct.3`, `prerelease=false`를 사용한다. canonical ct.3 이상의
정식 CrossTwins 버전만 허용하며 ct.1/ct.2 재실행 입력은 거부한다.

SDK gitlink는 `0bbc42b84f568fa70b0e17d66a8609e4e7fc4b44`, 플랫폼은 기존
pioarduino 55.03.311로 고정한다. 이를 바꾸려면 별도의 계약 검토가 필요하다.

## DAG와 실패 경계

`prepare → checks + build-release(5개 matrix) → seal`이다. checks는 formatter,
cppcheck, release/preflight Python 계약 및 전체 host/OTA 테스트를 실행한다.
기존 SectionSoftFlush 임시 파일 공유 때문에 host는 직렬로 실행한다.

matrix는 기존 `gh_release_rc`, `sticky-gh_release_rc`, `x4pro-gh_release_rc`,
`x4c-gh_release_rc`, `papermono-gh_release_rc`를 사용한다. 명칭 rc는 GitHub
pre-release 상태를 뜻하지 않는다. resolver의 `-ct.N` suffix를 주입한다.

정확한 chip ID/board marker/asset 이름, 첫 DROM app descriptor의 project/version,
ELF SHA, ESP checksum/append hash, app-only 전체 길이, 실제 compiled partitions와
원본 CSV의 OTA slot을 검사한다. unsigned `0xABCD5432`를 사용한다. 헤더만으로
같은 칩의 보드가 구분되는 것으로 간주하지 않는다. S3 chip ID 9는 4개 기기가
공유하므로 board marker도 필수다. X3/X4 공용은 chip ID 5와 marker `x4`다.

실패·취소·누락이 하나라도 있으면 최종 bundle이 생성되지 않는다. 개별 검증
artifact가 남더라도 그것을 전체 검증 성공이나 게시 허가로 취급하지 않는다.
artifact 이름은 run ID와 attempt를 포함한다. 부분 job 재실행은 이전 attempt
artifact를 가져오지 않아 실패할 수 있다. 재시도는 **모든 job 재실행**을 사용한다.

## 산출물

최종 artifact 이름은 `cross-twins-release-verified-<run_id>-<run_attempt>`다.
동일 attempt의 바이너리 5개, 기기별 검증 JSON 5개, `manifest.json`을 포함한다.
보존 기간은 30일이다. 장기 보존이 필요하면 만료 전에 내려받아 안전하게 보관한다.

manifest schema 1은 context(repository/tag/full SHA/workflow SHA/SDK SHA/run ID/
attempt/version/platform/mode) 및 artifacts(device/asset/size/raw SHA-256/ELF SHA/
chip/board/OTA limit/check 결과)를 기록한다. `seal`은 정확한 파일 집합, 동일 context,
raw SHA/크기 및 모든 검증 결과를 재확인한다. 기존 JSON은 덮어쓰지 않는다.
로컬 `--local` 결과는 mode=local이며 최종 `release_ready=false`다.

GitHub upload-artifact의 archive digest/size는 압축 포장에 대한 값이다.
Release raw `.bin` SHA-256은 manifest의 artifacts[].sha256이며 서로 대체하지 않는다.
manifest는 서명 또는 게시 권한을 의미하지 않는다. 생성한 workflow/run provenance도
반드시 확인해야 한다.

## 향후 게시 계약 — 아직 구현하지 않음

게시 자동화는 별도 승인 대상이다. 이번 workflow에는 게시 코드가 없다.
향후 publisher는 다음 조건을 충족해야 한다.

1. 입력 repository/run ID/attempt의 workflow 경로·정확한 workflow/source SHA·전체
   job 성공을 GitHub API로 확인한다. artifact 만료·취소·부분 실행을 거부한다.
2. mode=github-actions, release_ready=true, verification=passed, schema 1 및
   canonical tag/commit/SDK/기기 계약이 일치하는 manifest만 받는다.
3. 다운로드한 bundle의 정확한 5개 `.bin` 이름·크기·raw SHA를 manifest와 대조한다.
   artifact archive digest도 확인하되 raw hash 검증을 생략하지 않는다.
4. 승인된 태그의 peeled commit이 manifest commit과 같아야 한다. 존재하는 Release와
   asset은 수정·교체하지 않는다. 같은 이름 충돌은 실패이며 clobber를 사용하지 않는다.
5. 별도 승인 후 Draft에 **동일한 검증 바이너리**를 첨부하고 다운로드 SHA를 다시
   확인한 다음 정식 공개한다. 게시 단계에는 firmware rebuild가 없다.

새 workflow가 반영돼도 과거 commit의 Release workflow 및 기존 run을 소급 변경하지는
못한다. 과거 publish run을 재실행하면 기존 revision을 사용하므로 실행하지 않는다.
엄격한 과거 run 실행 차단은 저장소 권한/Actions 운영 정책 또는 별도 폐기 절차의
검토가 필요하다. 기존 ct.1/ct.2 태그를 이동해 이를 해결하지 않는다.

## 검증 한계

CI 성공은 실기기 OTA/부팅/복구를 보장하지 않는다. X3/X4 OTA wolfSSL 경로의
`setInsecure()` 및 `WOLFSSL_VERIFY_NONE`은 그대로다. 이미지 hash/allowlist와
사전 CI는 TLS 서버 인증과 펌웨어 서명을 대신하지 않는다.
