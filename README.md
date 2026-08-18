# PLEAT / STATE

> 주름은 장식이 아니라, 힘이 머문 기록인가?

**Pleat State**는 접힌 표면의 시작 조건과 장력·감쇠·중력의 관계를 브라우저에서 조작하는 계산 소재 연구다. 핵심 상태와 제약 해법은 C++20으로 작성하고 Emscripten으로 WebAssembly에 컴파일한다. JavaScript는 그 메모리를 읽어 2D Canvas에 그리며, 네이티브 CMake 빌드에서는 같은 코어를 별도로 시험한다.

- Live study: <https://jcy0908.github.io/pleat-state/> *(GitHub Pages 첫 배포 후 활성화)*
- Runtime: C++20 → WebAssembly
- Rendering: HTML Canvas 2D
- Build and test: CMake + CTest, GCC / Clang
- License: MIT

이 저장소의 목표는 실제 직물의 물성을 예측하는 것이 아니다. **질문 → 단순화한 모델 → 조작 가능한 증거 → 한계와 다음 실험**을 하나의 검토 가능한 작업물로 연결하는 것이 목표다.

## 범위

### 포함하는 것

- 위치 기반 입자망과 거리 제약
- 평면·주름·이완 상태를 만드는 초기 조건
- 장력, 감쇠, 중력, 횡력, 접힘 깊이, 접힘 수 조절
- 포인터 드래그와 키보드 입력
- WebAssembly 메모리의 위치·인덱스 버퍼를 복사 없이 읽는 렌더링 경계
- 네이티브 단위/회귀 시험과 GCC·Clang CI
- 모델 경계, 실패 기록, 대안 비교, AI 사용 범위의 명시

### 포함하지 않는 것

- 실측 소재에 맞춘 질량·탄성·마찰 계수
- 굽힘 강성, 섬유 방향성, 자기 충돌, 표면 충돌
- 정밀 공기역학 또는 3D 깊이 복원
- 제품 성능 벤치마크나 사람 대상 사용성 연구

따라서 화면의 수치는 **모델 내부의 상대적 변화**를 읽기 위한 진단값이다. 특히 `energy`는 SI 단위로 교정된 실제 에너지가 아니다.

## 구조

| 경로 | 책임 |
| --- | --- |
| `include/pleat/` | 벡터, 입자망, 제약과 공개 C++ 인터페이스 |
| `src/Cloth.cpp` | 적분, 제약 투영, 리셋, 입력과 진단값 |
| `src/wasm_bindings.cpp` | 브라우저에 노출하는 작고 명시적인 C ABI |
| `tests/` | 고정점, 유한 상태, 감쇠, 리셋과 입력 연속성 회귀 시험 |
| `web/` | 의미 구조, 기술 편집형 시각 체계, Canvas 렌더러와 조작 UI |
| `scripts/build-web.sh` | C++ 코어를 `dist/pleat.js`와 `dist/pleat.wasm`으로 빌드 |
| `.github/workflows/` | GCC·Clang 네이티브 CI와 GitHub Pages 배포 |

실행 경계는 의도적으로 작게 유지한다.

```text
controls → C ABI → C++ particle/constraint state
                          ↓ shared WASM memory
                 positions + triangle indices → Canvas
```

JavaScript가 별도의 물리 상태를 갖지 않으므로 네이티브 시험과 브라우저 표현이 같은 모델을 가리킨다. 대신 WebAssembly 선형 메모리가 커지면 기존 typed-array view가 무효가 될 수 있으므로, 웹 레이어는 버퍼 포인터를 다시 읽을 수 있어야 한다.

## 모델

각 입자 `i`는 현재 위치 `xᵢ`, 이전 위치 `xᵢ⁻`, 역질량 `wᵢ`를 가진다. 고정된 상단 경계는 `wᵢ = 0`이고 나머지는 외력과 제약에 반응한다. 한 단계의 개념적 흐름은 다음과 같다.

1. 감쇠된 위치 차이와 외력을 사용해 다음 위치를 예측한다. 감쇠 입력 `λ`는 프레임률에 종속된 단순 배율이 아니라 `d = exp(−λΔt)`로 적용한다.

   ```text
   pᵢ = xᵢ + d(xᵢ − xᵢ⁻) + aᵢ Δt²
   ```

2. 이웃 `i, j`의 거리 제약을 계산한다.

   ```text
   Cᵢⱼ(p) = ‖pᵢ − pⱼ‖ − ℓᵢⱼ
   ```

3. 역질량과 장력 계수를 사용해 두 위치를 제약 표면으로 투영한다. UI의 전체 장력 `s`는 8회 반복에 맞춰 `ŝ = 1 − (1 − s)^(1/8)`로 분배한다.

   ```text
   Δpᵢ ∝ −ŝ · wᵢ / (wᵢ + wⱼ) · Cᵢⱼ · nᵢⱼ
   ```

4. 제출된 프레임 간격을 최대 `1/20 s`로 제한해 누적하고, 내부적으로 고정된 `1/120 s` 단계로 진행한다. 한 호출에서 최대 6개 지연 단계를 따라잡으며 각 단계에서 제약을 8회 반복한다.

가로·세로 연결은 표면의 기본 길이를, 양방향 대각 연결은 셀의 전단 변형을 억제한다. 접힘 깊이 `A`와 접힘 수 `f`는 렌더링 텍스처가 아니라 아래와 같이 **입자망의 기준 형상**을 바꾼다.

```text
yrest = v · height + A · sin(2πfu) · (0.12 + 0.88v)
```

두 접힘 값이 바뀌면 이 형상에서 모든 제약의 기준 길이를 다시 계산하고 고정점을 복원한다. 이 방식은 조작과 결과의 관계를 빠르게 읽는 데 유리하지만, 실제 직물의 응력–변형률 곡선을 재현하지는 않는다.

## C / WebAssembly 경계

Emscripten 빌드는 다음 심볼만 보존한다.

| 역할 | 함수 |
| --- | --- |
| 생명주기 | `ps_create`, `ps_destroy`, `ps_reset` |
| 진행 | `ps_step`, `ps_set_params` |
| 입력 | `ps_pointer_down`, `ps_pointer_move`, `ps_pointer_up` |
| 기하 버퍼 | `ps_particle_count`, `ps_positions_ptr`, `ps_index_count`, `ps_indices_ptr` |
| 진단 | `ps_energy` |

`ps_positions_ptr()`는 연속된 `float` 위치 배열, `ps_indices_ptr()`는 삼각형 인덱스 배열을 가리킨다. 소유권은 C++ 런타임에 있으며 JavaScript는 이 포인터를 해제하지 않는다. `ps_create()` 이후 또는 메모리 성장 이후에는 포인터와 typed-array view를 다시 얻는 것이 안전하다.

## 네이티브 빌드와 시험

요구 사항:

- CMake 3.20 이상
- C++20을 지원하는 GCC 또는 Clang

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

디버그 빌드가 필요하면 별도 디렉터리를 사용한다.

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
```

CI는 같은 명령을 GCC와 Clang에서 각각 실행하고, 별도 작업에서 Emscripten 번들도 생성해 세 필수 산출물을 확인한다. 테스트 통과는 모델의 안정성과 회귀 방지에 대한 최소 증거이며, 실제 소재에 대한 정확성을 증명하지 않는다.

## WebAssembly 빌드

[Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)를 설치하고 `em++`가 현재 셸의 `PATH`에 있는지 확인한다.

```bash
./scripts/build-web.sh
```

스크립트는 `web/`의 정적 파일을 `dist/`로 복사하고 아래 산출물을 만든다.

```text
dist/
├── index.html
├── app.js
├── styles.css
├── pleat.js
└── pleat.wasm
```

WebAssembly는 `file://`에서 직접 열지 않는다. 올바른 fetch 경로와 `application/wasm` MIME 처리를 위해 반드시 HTTP 서버로 확인한다.

```bash
python3 -m http.server 8080 --directory dist
```

그다음 <http://localhost:8080>을 연다. 프로덕션 배포는 `main` 브랜치의 Pages 워크플로가 같은 빌드 스크립트를 실행하므로 로컬과 CI의 컴파일 경로가 갈라지지 않는다. 저장소 설정의 **Pages → Build and deployment → Source**는 **GitHub Actions**여야 한다.

## 조작과 접근성

- 포인터: 망을 잡아 끌어 국소 변형을 만든다.
- 키보드: Canvas에 초점을 둔 뒤 방향키로 조작 지점을 이동하고 Space로 잡기/놓기를 전환한다.
- 버튼: 재생/일시정지, 초기화, 세 가지 상태 프리셋을 제공한다.
- 범위 입력: 각 값은 연결된 텍스트 출력으로도 표시된다.
- 문서 탐색: 본문 바로가기 링크, 의미 있는 제목 구조와 내비게이션 랜드마크를 사용한다.
- 상태: 런타임 준비/실패 상태는 `aria-live` 텍스트로 전달한다.
- 모션: 사용자의 `prefers-reduced-motion` 설정을 존중한다.

Canvas의 모든 순간적 형상을 화면 낭독기에 완전히 전달하지는 못한다. 모델 설명과 실시간 수치가 보조 경로를 제공하지만 동등한 촉각·비시각 표현은 아직 해결하지 못한 과제다.

## 디자인 비평

시각 체계는 패션 브랜드의 표면 스타일을 복제하지 않고, 그 계보에서 공통으로 읽히는 절제·재료성·구조의 가시화를 기술 편집 문법으로 번역한다. 따뜻한 종이색, 높은 대비의 잉크색, 작은 신호색, 엄격한 그리드와 무광의 2D 메시를 사용한다.

가장 강한 점은 계산을 장면 뒤에 숨기지 않고 질문·제어·수치·구현 경계를 한 화면에 놓는다는 점이다. 가장 큰 약점은 2D 입자망이 실제 천처럼 보일수록 모델의 정확도까지 높다고 오해할 수 있다는 점이다. 이를 줄이기 위해 광택과 과도한 깊이 효과를 피하고, 물성 검증을 하지 않았다는 경계를 화면과 문서에 반복해 적는다.

다음 단계는 실제 주름 샘플의 접힘 간격과 복귀 시간을 촬영하고, 절대값을 맞추기 전에 모델이 변화 **방향**을 일관되게 설명하는지 비교하는 것이다.

## 저작과 AI 사용 공개

이 저장소의 초기 구조, 구현 대안 탐색, 코드와 문장 초안, 정적 검토에는 OpenAI Codex가 보조 도구로 사용되었다. 생성된 제안을 사실이나 연구 결과로 간주하지 않으며, 자동 시험과 소스 검토가 가능한 형태로 남긴다. 수행하지 않은 사용자 시험, 소재 측정, 성능 비교는 만들어내지 않았다.

사용된 도구와 무관하게 저장소를 배포·수정하는 관리자가 최종 동작, 주장, 라이선스와 접근성에 대한 책임을 가진다.

## 1차·공식 참고자료

- Müller, Heidelberger, Hennix, Ratcliff, [*Position Based Dynamics*](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf) — 위치 예측과 제약 투영의 원 논문
- Emscripten, [Compiler settings reference](https://emscripten.org/docs/tools_reference/settings_reference.html) — `MODULARIZE`, `EXPORT_NAME` 등 빌드 설정
- Emscripten, [FAQ: exporting functions](https://emscripten.org/docs/getting_started/FAQ.html#how-do-i-tell-the-compiler-that-a-function-will-be-called-from-javascript) — 제거되지 않아야 하는 C 심볼 공개
- Emscripten, [Optimizing code: C++ exceptions](https://emscripten.org/docs/optimizing/Optimizing-Code.html#c-exceptions) — 최적화 빌드에서 생성자 검증 예외를 포착하기 위한 설정
- CMake, [`cmake(1)` manual](https://cmake.org/cmake/help/latest/manual/cmake.1.html) 및 [`ctest(1)` manual](https://cmake.org/cmake/help/latest/manual/ctest.1.html) — 네이티브 빌드와 시험
- W3C, [WebAssembly Core Specification](https://www.w3.org/TR/wasm-core-2/) — WebAssembly 실행 형식
- GitHub Docs, [Using custom workflows with GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages) — Pages artifact와 배포 워크플로
- W3C, [Web Content Accessibility Guidelines (WCAG) 2.2](https://www.w3.org/TR/WCAG22/) — 키보드, 상태 전달과 모션 설계 기준

외부 시뮬레이션 코드를 복사하거나 런타임 의존성으로 포함하지 않았다. 논문은 모델의 출발점을 설명하는 참고자료이며, 현재 구현이 논문의 모든 항목을 재현한다는 뜻은 아니다.

## License

[MIT](./LICENSE) © 2026 jcy0908
