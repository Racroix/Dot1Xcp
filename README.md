# Dot1xCP (Windows Credential Provider)

## 1. 목적
Dot1xCP는 Windows LogonUI에 커스텀 타일을 제공하고, 실제 네트워크 인증 UI/로직은 외부 프로세스(`Dot1xBroker.exe`)로 위임하는 Credential Provider입니다.

핵심 목표:
- 로컬 Windows 로그온과 Dot1x 네트워크 인증을 같은 사용자 입력 기준으로 연결
- CP 자체는 최소 UI/제어만 담당하고 민감 처리와 상태 관리는 Broker에 위임
- 실패 시 CP가 멈추지 않고 다시 인증 시도를 유도

## 2. 현재 동작 범위
- 지원 시나리오: `CPUS_LOGON`만 지원
- 미지원 시나리오: `CPUS_UNLOCK_WORKSTATION`는 `E_NOTIMPL`
- 타일 수: 1개 고정
- 제출 버튼: 필드는 존재하지만 UI에서는 숨김(`FI_SUBMIT -> CPFS_HIDDEN`)
- 자동 제출: 타일 선택 시 내부 상태에 따라 자동 제출 플래그(`pbAutoLogon`) 반환

참고 파일:
- `Provider.cpp`
- `ProviderCredentialFields.cpp`
- `Provider.h`

## 3. 아키텍처 요약
CP는 다음 역할을 수행합니다.

1. 타일 노출 및 선택/재선택 제어
2. Broker 프로세스 실행
3. Named Pipe 수신 서버 생성 및 Broker 메시지 수신
4. Broker 응답(JSON)을 Windows Credential Serialization으로 변환
5. 실패/타임아웃/프로세스 종료 시 복구 루프 유지

핵심 파일:
- `ProviderSerialization.cpp`: GetSerialization 메인 상태머신
- `ProviderPipe.cpp`: Pipe 생성, 연결 대기, read timeout/exit 감시, Broker 실행
- `ProviderJson.cpp`: JSON 파싱 및 민감 문자열 clear
- `Serialization.cpp`: LSA 패키지 직렬화(MSV/Kerberos)

## 4. CP ↔ Broker 프로토콜
CP는 Broker에서 아래 메시지를 수신합니다.

- `AUTH_PENDING`
  - 의미: 인증 진행 중
  - 동작: `CPGSR_NO_CREDENTIAL_NOT_FINISHED` 반환, 빠른 주기로 `CredentialsChanged` 재요청
- `AUTH_IDLE`
  - 의미: 브로커 대기 상태(재시도 대기 등)
  - 동작: `CPGSR_NO_CREDENTIAL_NOT_FINISHED` 반환, 느린 주기로 `CredentialsChanged` 재요청
- `AUTH_SUCCESS` (`sam`, `password`)
  - 의미: 인증 승인
  - 동작: `Ser::PackAutoEx`로 직렬화 후 `CPGSR_RETURN_CREDENTIAL_FINISHED`
- `AUTH_FAIL`
  - 의미: 인증 실패
  - 동작: `CPGSR_NO_CREDENTIAL_NOT_FINISHED`로 타일 유지
- `AUTH_FALLBACK`
  - 의미: ESC 등으로 기본 자격증명 로그인 경로 선택
  - 동작: `CPGSR_NO_CREDENTIAL_FINISHED`

## 5. 세션/복구 로직
- Pipe connect 실패/timeout/broker 종료를 상태별로 분리 처리
- Broker가 예기치 않게 종료되면 즉시 실패 종료 대신 자동 복구 루프 유지
- `ArmForceNextAutoSubmit()` + `RequestCredentialsChangedAsync()`로 다음 재진입을 유도
- Pending/Idle 흐름에서는 프로세스를 정리하지 않고 세션 유지

## 6. 직렬화 처리 핵심
`Serialization.cpp` 기준:
- 로컬 계정(domain `"."`)은 MSV(`MICROSOFT_AUTHENTICATION_PACKAGE_V1_0`) 경로
- 도메인 계정은 Kerberos 경로
- Unlock 시나리오용 LUID 처리 코드가 있으나, Provider 정책상 현재 Unlock 시나리오는 노출하지 않음

중요 구현 포인트:
- `UNICODE_STRING.Buffer`에 raw 포인터가 아니라 BLOB 기준 오프셋 포인터를 넣는 처리
- 이 부분이 틀리면 LSA 쪽에서 직렬화 실패/로그온 실패가 발생

## 7. 로그 및 경로
- CP 로그: `C:\ProgramData\Dot1xCP\cp.log`
- Broker 실행 경로(기본): `C:\Program Files\Dot1xCP\Broker\Dot1xBroker.exe`
- CP CLSID: `{7C5B9DC9-3FAE-4E3C-8B1F-2B4B530A1D77}`

## 8. 운영 시 주의사항
- 잠금화면에서 사용자 타일 노출 정책은 CP만으로 강제 제어할 수 없음
- 잠금화면에서 마지막 사용자 위주 UX가 필요하면 OS 정책(`HideFastUserSwitching` 등) 병행 필요
- CP가 응답을 오래 잡고 있으면 사용자 체감상 멈춤으로 보이므로 Pending/Idle 주기 설계가 중요
- Broker 응답 메시지 JSON 형식이 바뀌면 CP 파싱 실패로 즉시 재시도 루프로 떨어짐
- 경로 하드코딩(`Program Files\Dot1xCP\Broker`) 변경 시 설치 스크립트와 동기화 필요

## 9. 실제 시행착오/문제 사례
1. `no response from broker` / `ERROR_BROKEN_PIPE`
- 원인: Broker 예외 종료, Pipe write/read 타이밍 경합
- 조치: 종료를 즉시 실패 처리하지 않고 자동 복구 루프 유지, 세션 상태 분리

2. Pending/Idle 과다 로그로 원인 추적 어려움
- 원인: 고빈도 `CredentialsChanged` 재호출 구간의 로그 스팸
- 조치: 상태 전이 시점 로그 중심으로 축소

3. 잠금 시나리오 기대 불일치
- 원인: Unlock 시나리오를 CP에서 지원하지 않음(`E_NOTIMPL`)
- 조치: 정책 기반 제어를 설치 단계에서 병행

4. 직렬화 구조체 실패
- 원인: `UNICODE_STRING.Buffer` 오프셋 처리 오류
- 조치: BLOB 기준 오프셋 포인터로 수정

## 10. 점검 체크리스트
- CP가 LOGON에서만 활성화되는지 확인
- Broker launch/connect/read timeout 로그가 의도대로 남는지 확인
- `AUTH_PENDING -> AUTH_SUCCESS/AUTH_FAIL` 전이가 cp.log에 맞게 기록되는지 확인
- 성공 시 `CPGSR_RETURN_CREDENTIAL_FINISHED` 경로로 정상 진입하는지 확인

## 11. 빌드/배포 최소 절차
기준 경로: 이 README가 있는 저장소 루트(`Dot1xCP/`)

1. CP 빌드
- Visual Studio: `x64` + `Release`
- 또는 명령:
```bat
msbuild Dot1xCP.sln /p:Configuration=Release /p:Platform=x64
```
- 산출물: `x64\Release\Dot1xCP.dll`

2. Broker 빌드
- Broker 소스가 별도 저장소/폴더인 경우, 해당 루트에서 실행:
```bat
dotnet publish Dot1xBroker.csproj -c Release -f net8.0-windows
```
- 산출물: `bin\Release\net8.0-windows\`

3. 설치
- 설치 패키징(`Dot1xCP_Setup.iss`)은 설치 스크립트 저장소/폴더에서 수행
- 설치 경로 기준:
  - CP: `C:\Program Files\Dot1xCP\Dot1xCP.dll`
  - Broker: `C:\Program Files\Dot1xCP\Broker\`

## 12. 운영 시나리오 기대 동작
1. 부팅 후 로그온 화면
- CP 타일 선택/자동 제출로 Broker 실행
- 인증 성공 시 즉시 Windows 로그온까지 완료

2. 인증 실패
- Broker UI 유지
- 재시도 백오프(5초 단위 증가)
- CP는 `NO_CREDENTIAL_NOT_FINISHED`로 세션 유지

3. ESC fallback
- Broker가 `AUTH_FALLBACK` 전송
- CP는 기본 Windows 자격증명 경로로 반환

4. Broker 비정상 종료
- CP는 즉시 hard-fail 대신 자동 회복 경로로 재진입 유도

## 13. 트러블슈팅(운영자용)
| 증상 | 우선 확인 | 원인 후보 | 조치 |
|---|---|---|---|
| `no response from broker` | `cp.log`, `broker.log` 동시 확인 | broker crash / pipe 끊김 / 메시지 미전송 | broker 예외 로그 확인, pipe 경로/권한 확인 |
| `broker exited unexpectedly` | `cp.log`의 read/connect 단계 | launch 실패 또는 조기 종료 | broker 실행 파일 경로/의존성(.NET, VC++) 확인 |
| 인증 성공했는데 로그온 미완료 | `AUTH_SUCCESS` 수신 여부 | serialization 실패 또는 타이밍 경합 | `Serialization.cpp` 경로/NTSTATUS 로그 확인 |
| 잠금 화면에서 사용자 다중 노출 | 정책값 확인 | OS 정책 미적용 | 설치 정책(`HideFastUserSwitching`) 적용 여부 확인 |

참고:
- Broker 설정 샘플은 `Dot1xBroker/README.md`의 `appsettings.json` 섹션 참조.

## 14. 별도 처리 항목(분리 추적)
- 이슈: Dot1xBroker가 장시간 실행된 상태에서 인증 성공 후 로그온은 완료되지만, 특정 타이밍에 CP가 Broker를 다시 실행하는 현상이 관찰됨
- 현재 분류: 별도 처리(재현 조건/로그 패턴 우선 확정)
- 우선 확인 로그:
  - `cp.log`: `AUTH_SUCCESS -> RETURN_CREDENTIAL_FINISHED` 직후 `GetSerialization: broker session started.` 재출력 여부
  - `broker.log`: 동일 시각 신규 프로세스 시작 흔적 여부
- 점검 포인트:
  - 성공 직전 예약된 `CredentialsChanged` 콜백 잔존 여부
  - 세션 종료 시 Broker 프로세스/핸들 정리 완료 여부
  - 타일 재선택(`SetSelected`) 경로에서 자동 제출 재진입 여부
