const canvas = document.querySelector("#pleat-canvas");
const context = canvas.getContext("2d", { alpha: false });
const instrument = document.querySelector(".instrument");
const runtimeStatus = document.querySelector("#runtime-status");
const interactionStatus = document.querySelector("#interaction-status");
const pauseButton = document.querySelector("#pause-button");
const resetButton = document.querySelector("#reset-button");
const presetButtons = [...document.querySelectorAll("[data-preset]")];

const metricNodes = document.querySelector("#metric-nodes");
const metricFaces = document.querySelector("#metric-faces");
const metricEnergy = document.querySelector("#metric-energy");
const metricRender = document.querySelector("#metric-render");

const WIDTH = 960;
const HEIGHT = 620;
const COLUMNS = 31;
const ROWS = 21;
const CLOTH_WIDTH = 860;
const CLOTH_HEIGHT = 440;
const ORIGIN = { x: 50, y: 66 };
const PARTICLE_COUNT = COLUMNS * ROWS;
const prefersReducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

const parameterNames = ["stiffness", "damping", "gravity", "wind", "amplitude", "frequency"];
const parameterInputs = Object.fromEntries(
  parameterNames.map((name) => [name, document.querySelector(`#${name}`)]),
);

const presets = {
  flat: {
    stiffness: 0.9,
    damping: 4.4,
    gravity: 520,
    wind: 0,
    amplitude: 0,
    frequency: 6,
  },
  pleated: {
    stiffness: 0.82,
    damping: 3.2,
    gravity: 720,
    wind: 0,
    amplitude: 56,
    frequency: 9,
  },
  released: {
    stiffness: 0.4,
    damping: 1.6,
    gravity: 960,
    wind: 80,
    amplitude: 24,
    frequency: 6,
  },
};

let runtime = null;
let mode = "preview";
let running = !prefersReducedMotion.matches;
let dirty = true;
let lastFrameTime = performance.now();
let lastTelemetryTime = 0;
let fallbackIndices = new Uint32Array();
let theme = readTheme();

const pointer = {
  x: CLOTH_WIDTH * 0.5,
  y: CLOTH_HEIGHT * 0.52,
  active: false,
  grabbed: false,
  visible: false,
  pointerId: null,
};

function readTheme() {
  const styles = getComputedStyle(document.documentElement);
  return {
    paper: styles.getPropertyValue("--paper").trim() || "#efeee9",
    paperDeep: styles.getPropertyValue("--paper-deep").trim() || "#e4e2da",
    ink: styles.getPropertyValue("--ink").trim() || "#181917",
    muted: styles.getPropertyValue("--muted").trim() || "#62635e",
    line: styles.getPropertyValue("--line").trim() || "#b9b8b1",
    lineStrong: styles.getPropertyValue("--line-strong").trim() || "#777873",
    signal: styles.getPropertyValue("--signal").trim() || "#a43b2b",
    signalSoft: styles.getPropertyValue("--signal-soft").trim() || "#ddc1ba",
  };
}

function setRuntimeStatus(kind, message) {
  instrument.classList.remove("is-live", "is-preview");
  instrument.classList.add(kind === "live" ? "is-live" : "is-preview");
  runtimeStatus.textContent = message;
}

function announce(message) {
  interactionStatus.textContent = "";
  window.setTimeout(() => {
    interactionStatus.textContent = message;
  }, 20);
}

function formatParameter(name, value) {
  if (name === "stiffness") return value.toFixed(2);
  if (name === "damping") return value.toFixed(1);
  return Math.round(value).toString();
}

function readParameters() {
  return Object.fromEntries(
    parameterNames.map((name) => [name, Number.parseFloat(parameterInputs[name].value)]),
  );
}

function updateOutputs() {
  const values = readParameters();
  parameterNames.forEach((name) => {
    const output = document.querySelector(`#${name}-output`);
    output.value = formatParameter(name, values[name]);
    output.textContent = output.value;
  });
}

function applyParameters() {
  updateOutputs();
  const values = readParameters();
  if (runtime) {
    runtime.setParams(
      values.stiffness,
      values.damping,
      values.gravity,
      values.wind,
      values.amplitude,
      values.frequency,
    );
  }
  dirty = true;
}

function setControls(values) {
  parameterNames.forEach((name) => {
    parameterInputs[name].value = String(values[name]);
  });
  applyParameters();
}

function setPreset(name, shouldAnnounce = true) {
  const preset = presets[name];
  if (!preset) return;

  presetButtons.forEach((button) => {
    const selected = button.dataset.preset === name;
    button.classList.toggle("is-active", selected);
    button.setAttribute("aria-pressed", String(selected));
  });
  setControls(preset);
  runtime?.reset();
  dirty = true;
  if (shouldAnnounce) {
    announce(`${name === "flat" ? "평면" : name === "pleated" ? "주름" : "이완"} 상태를 적용했습니다.`);
  }
}

function updatePauseInterface() {
  const paused = !running;
  pauseButton.setAttribute("aria-pressed", String(paused));
  pauseButton.textContent = paused ? "계속" : "일시정지";

  if (mode === "live") {
    setRuntimeStatus("live", paused ? "WASM 준비됨 · 일시정지" : "WASM 실행 중 · C++20 core");
  }
}

function resizeCanvas() {
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  const targetWidth = Math.round(WIDTH * ratio);
  const targetHeight = Math.round(HEIGHT * ratio);
  if (canvas.width !== targetWidth || canvas.height !== targetHeight) {
    canvas.width = targetWidth;
    canvas.height = targetHeight;
  }
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  dirty = true;
}

function buildFallbackIndices() {
  const indices = [];
  for (let row = 0; row < ROWS - 1; row += 1) {
    for (let column = 0; column < COLUMNS - 1; column += 1) {
      const upperLeft = row * COLUMNS + column;
      const upperRight = upperLeft + 1;
      const lowerLeft = upperLeft + COLUMNS;
      const lowerRight = lowerLeft + 1;
      indices.push(upperLeft, lowerLeft, upperRight, upperRight, lowerLeft, lowerRight);
    }
  }
  fallbackIndices = Uint32Array.from(indices);
}

function fallbackGeometry() {
  const values = readParameters();
  const positions = new Float32Array(PARTICLE_COUNT * 2);
  const amplitude = values.amplitude * (0.42 + values.stiffness * 0.58);
  const gravitySag = (values.gravity / 1400) * 34 * (1 - values.stiffness * 0.28);
  const lateralShift = (values.wind / 600) * 28;

  for (let row = 0; row < ROWS; row += 1) {
    const v = row / (ROWS - 1);
    for (let column = 0; column < COLUMNS; column += 1) {
      const u = column / (COLUMNS - 1);
      const index = (row * COLUMNS + column) * 2;
      const fold = amplitude * Math.sin(u * values.frequency * Math.PI * 2) * (0.12 + 0.88 * v);
      positions[index] = u * CLOTH_WIDTH + lateralShift * v * v;
      positions[index + 1] = v * CLOTH_HEIGHT + fold + gravitySag * v * v;
    }
  }

  return {
    positions,
    indices: fallbackIndices,
    particleCount: PARTICLE_COUNT,
    indexCount: fallbackIndices.length,
    energy: null,
  };
}

function runtimeGeometry() {
  if (!runtime) return fallbackGeometry();
  try {
    const particleCount = runtime.particleCount();
    const indexCount = runtime.indexCount();
    const positionsPointer = runtime.positionsPointer();
    const indicesPointer = runtime.indicesPointer();

    if (
      particleCount < 1 ||
      indexCount < 3 ||
      particleCount > 200000 ||
      indexCount > 1200000 ||
      !positionsPointer ||
      !indicesPointer ||
      !runtime.module.HEAPF32 ||
      !runtime.module.HEAPU32
    ) {
      throw new Error("Invalid WebAssembly geometry view");
    }

    const positions = runtime.module.HEAPF32.subarray(
      positionsPointer >>> 2,
      (positionsPointer >>> 2) + particleCount * 2,
    );
    const indices = runtime.module.HEAPU32.subarray(
      indicesPointer >>> 2,
      (indicesPointer >>> 2) + indexCount,
    );

    return {
      positions,
      indices,
      particleCount,
      indexCount,
      energy: runtime.energy(),
    };
  } catch (error) {
    console.warn("Pleat State switched to its explanatory preview.", error);
    activatePreview("WASM 메모리 뷰를 읽지 못해 설명용 정적 프리뷰를 표시합니다");
    return fallbackGeometry();
  }
}

function validPoint(positions, index) {
  const x = positions[index * 2];
  const y = positions[index * 2 + 1];
  return Number.isFinite(x) && Number.isFinite(y) ? [x, y] : null;
}

function appendTriangle(path, positions, first, second, third) {
  const a = validPoint(positions, first);
  const b = validPoint(positions, second);
  const c = validPoint(positions, third);
  if (!a || !b || !c) return;
  path.moveTo(a[0], a[1]);
  path.lineTo(b[0], b[1]);
  path.lineTo(c[0], c[1]);
  path.closePath();
}

function drawReferenceFrame() {
  context.save();
  context.translate(ORIGIN.x, ORIGIN.y);
  context.strokeStyle = theme.line;
  context.lineWidth = 1;
  context.strokeRect(0, 0, CLOTH_WIDTH, CLOTH_HEIGHT);

  context.beginPath();
  for (let index = 0; index <= 10; index += 1) {
    const x = (CLOTH_WIDTH / 10) * index;
    context.moveTo(x, -7);
    context.lineTo(x, 0);
  }
  for (let index = 0; index <= 5; index += 1) {
    const y = (CLOTH_HEIGHT / 5) * index;
    context.moveTo(-7, y);
    context.lineTo(0, y);
  }
  context.stroke();

  context.fillStyle = theme.muted;
  context.font = '9px "SFMono-Regular", Consolas, monospace';
  context.textBaseline = "bottom";
  context.fillText("PINNED EDGE / 00", 0, -10);
  context.restore();
}

function drawMesh(geometry) {
  const { positions, indices, particleCount, indexCount } = geometry;
  const softFaces = new Path2D();
  const signalFaces = new Path2D();
  const triangleCount = Math.floor(indexCount / 3);

  for (let triangle = 0; triangle < triangleCount; triangle += 1) {
    const offset = triangle * 3;
    const first = indices[offset];
    const second = indices[offset + 1];
    const third = indices[offset + 2];
    if (first >= particleCount || second >= particleCount || third >= particleCount) continue;
    const target = triangle % 19 === 0 || triangle % 19 === 1 ? signalFaces : softFaces;
    appendTriangle(target, positions, first, second, third);
  }

  context.save();
  context.translate(ORIGIN.x, ORIGIN.y);
  context.fillStyle = theme.paperDeep;
  context.fill(softFaces);
  context.fillStyle = theme.signalSoft;
  context.fill(signalFaces);

  context.strokeStyle = theme.lineStrong;
  context.lineWidth = 0.65;
  context.beginPath();
  for (let row = 0; row < ROWS; row += 1) {
    for (let column = 0; column < COLUMNS; column += 1) {
      const point = validPoint(positions, row * COLUMNS + column);
      if (!point) continue;
      if (column === 0) context.moveTo(point[0], point[1]);
      else context.lineTo(point[0], point[1]);
    }
  }
  for (let column = 0; column < COLUMNS; column += 1) {
    for (let row = 0; row < ROWS; row += 1) {
      const point = validPoint(positions, row * COLUMNS + column);
      if (!point) continue;
      if (row === 0) context.moveTo(point[0], point[1]);
      else context.lineTo(point[0], point[1]);
    }
  }
  context.stroke();

  context.fillStyle = theme.signal;
  for (let column = 0; column < COLUMNS; column += 3) {
    const point = validPoint(positions, column);
    if (!point) continue;
    context.fillRect(point[0] - 2, point[1] - 2, 4, 4);
  }
  context.restore();
}

function drawPointer() {
  if (!pointer.visible && !pointer.active) return;
  const x = ORIGIN.x + pointer.x;
  const y = ORIGIN.y + pointer.y;
  context.save();
  context.strokeStyle = pointer.active ? theme.signal : theme.ink;
  context.lineWidth = pointer.active ? 1.5 : 1;
  context.beginPath();
  context.moveTo(x - 10, y);
  context.lineTo(x + 10, y);
  context.moveTo(x, y - 10);
  context.lineTo(x, y + 10);
  context.stroke();
  context.strokeRect(x - 4, y - 4, 8, 8);
  context.restore();
}

function formatEnergy(value) {
  if (value === null || !Number.isFinite(value)) return "N/A";
  const magnitude = Math.abs(value);
  if (magnitude === 0) return "0.00";
  if (magnitude >= 100000 || magnitude < 0.01) return value.toExponential(2);
  return value.toLocaleString("ko-KR", { maximumFractionDigits: 2 });
}

function render(now) {
  const renderStart = performance.now();
  const geometry = runtimeGeometry();
  context.fillStyle = theme.paper;
  context.fillRect(0, 0, WIDTH, HEIGHT);
  drawReferenceFrame();
  drawMesh(geometry);
  drawPointer();

  const renderCost = performance.now() - renderStart;
  if (now - lastTelemetryTime > 220 || lastTelemetryTime === 0) {
    metricNodes.textContent = geometry.particleCount.toLocaleString("ko-KR");
    metricFaces.textContent = Math.floor(geometry.indexCount / 3).toLocaleString("ko-KR");
    metricEnergy.textContent = formatEnergy(geometry.energy);
    metricRender.textContent = `${renderCost.toFixed(1)} ms`;
    lastTelemetryTime = now;
  }
}

function frame(now) {
  const elapsed = Math.min(Math.max((now - lastFrameTime) / 1000, 0), 0.05);
  lastFrameTime = now;

  if (runtime && running) {
    runtime.step(elapsed);
    dirty = true;
  }

  if (dirty) {
    render(now);
    dirty = false;
  }
  window.requestAnimationFrame(frame);
}

function eventPosition(event) {
  const bounds = canvas.getBoundingClientRect();
  return {
    x: (event.clientX - bounds.left) * (WIDTH / bounds.width) - ORIGIN.x,
    y: (event.clientY - bounds.top) * (HEIGHT / bounds.height) - ORIGIN.y,
  };
}

function clampPointer() {
  pointer.x = Math.min(Math.max(pointer.x, 0), CLOTH_WIDTH);
  pointer.y = Math.min(Math.max(pointer.y, 0), CLOTH_HEIGHT);
}

function releasePointer() {
  if (pointer.grabbed) runtime?.pointerUp();
  pointer.active = false;
  pointer.grabbed = false;
  pointer.pointerId = null;
  dirty = true;
}

function activatePreview(message) {
  if (runtime) {
    try {
      runtime.destroy();
    } catch (error) {
      console.warn("Could not release the previous WebAssembly instance.", error);
    }
  }
  runtime = null;
  mode = "preview";
  running = false;
  pauseButton.disabled = true;
  pauseButton.textContent = "정적 보기";
  pauseButton.setAttribute("aria-pressed", "true");
  setRuntimeStatus("preview", message || "WASM 미연결 · 설명용 정적 프리뷰");
  metricEnergy.textContent = "N/A";
  dirty = true;
}

function bindRuntime(module) {
  const cwrap = module.cwrap.bind(module);
  const api = {
    module,
    create: cwrap("ps_create", "number", ["number", "number", "number", "number"]),
    destroy: cwrap("ps_destroy", null, []),
    reset: cwrap("ps_reset", null, []),
    step: cwrap("ps_step", null, ["number"]),
    setParams: cwrap("ps_set_params", null, ["number", "number", "number", "number", "number", "number"]),
    pointerDown: cwrap("ps_pointer_down", "number", ["number", "number", "number"]),
    pointerMove: cwrap("ps_pointer_move", null, ["number", "number"]),
    pointerUp: cwrap("ps_pointer_up", null, []),
    particleCount: cwrap("ps_particle_count", "number", []),
    positionsPointer: cwrap("ps_positions_ptr", "number", []),
    indexCount: cwrap("ps_index_count", "number", []),
    indicesPointer: cwrap("ps_indices_ptr", "number", []),
    energy: cwrap("ps_energy", "number", []),
  };

  if (api.create(COLUMNS, ROWS, CLOTH_WIDTH, CLOTH_HEIGHT) !== 1) {
    throw new Error("The C++ cloth instance could not be created");
  }
  return api;
}

async function initialiseWebAssembly() {
  if (typeof window.createPleatModule !== "function") {
    activatePreview("WASM 번들을 찾지 못해 설명용 정적 프리뷰를 표시합니다");
    return;
  }

  try {
    const module = await window.createPleatModule({ noInitialRun: true });
    runtime = bindRuntime(module);
    mode = "live";
    pauseButton.disabled = false;
    running = !prefersReducedMotion.matches;
    applyParameters();
    runtime.reset();
    updatePauseInterface();
    dirty = true;
  } catch (error) {
    console.warn("Pleat State WebAssembly did not initialise.", error);
    activatePreview("WASM 초기화에 실패해 설명용 정적 프리뷰를 표시합니다");
  }
}

parameterNames.forEach((name) => {
  parameterInputs[name].addEventListener("input", () => {
    presetButtons.forEach((button) => {
      button.classList.remove("is-active");
      button.setAttribute("aria-pressed", "false");
    });
    applyParameters();
  });
});

presetButtons.forEach((button) => {
  button.setAttribute("aria-pressed", String(button.classList.contains("is-active")));
  button.addEventListener("click", () => setPreset(button.dataset.preset));
});

pauseButton.addEventListener("click", () => {
  if (!runtime) return;
  running = !running;
  lastFrameTime = performance.now();
  dirty = true;
  updatePauseInterface();
  announce(running ? "시뮬레이션을 계속합니다." : "시뮬레이션을 일시정지했습니다.");
});

resetButton.addEventListener("click", () => {
  if (runtime) {
    runtime.reset();
    announce("현재 설정의 시작 형상으로 되돌렸습니다.");
  } else {
    setPreset("pleated", false);
    announce("설명용 프리뷰를 기본 주름 상태로 되돌렸습니다.");
  }
  dirty = true;
});

canvas.addEventListener("pointerdown", (event) => {
  if (!event.isPrimary || (event.pointerType === "mouse" && event.button !== 0)) return;
  event.preventDefault();
  const position = eventPosition(event);
  pointer.x = position.x;
  pointer.y = position.y;
  clampPointer();
  pointer.active = true;
  pointer.visible = true;
  pointer.pointerId = event.pointerId;
  pointer.grabbed = Boolean(runtime?.pointerDown(pointer.x, pointer.y, 52));
  canvas.setPointerCapture(event.pointerId);
  dirty = true;

  if (!runtime) announce("현재는 설명용 정적 프리뷰입니다. WebAssembly 빌드에서 힘을 적용할 수 있습니다.");
});

canvas.addEventListener("pointermove", (event) => {
  if (event.pointerId !== pointer.pointerId) return;
  const position = eventPosition(event);
  pointer.x = position.x;
  pointer.y = position.y;
  clampPointer();
  if (pointer.grabbed) runtime?.pointerMove(pointer.x, pointer.y);
  dirty = true;
});

canvas.addEventListener("pointerup", (event) => {
  if (event.pointerId !== pointer.pointerId) return;
  releasePointer();
});

canvas.addEventListener("pointercancel", releasePointer);
canvas.addEventListener("lostpointercapture", releasePointer);

canvas.addEventListener("focus", () => {
  pointer.visible = true;
  dirty = true;
});

canvas.addEventListener("blur", () => {
  releasePointer();
  pointer.visible = false;
  dirty = true;
});

canvas.addEventListener("keydown", (event) => {
  const direction = {
    ArrowLeft: [-1, 0],
    ArrowRight: [1, 0],
    ArrowUp: [0, -1],
    ArrowDown: [0, 1],
  }[event.key];

  if (direction) {
    event.preventDefault();
    const distance = event.shiftKey ? 30 : 14;
    pointer.x += direction[0] * distance;
    pointer.y += direction[1] * distance;
    clampPointer();
    if (pointer.grabbed) runtime?.pointerMove(pointer.x, pointer.y);
    pointer.visible = true;
    dirty = true;
    return;
  }

  if (event.code === "Space") {
    event.preventDefault();
    if (pointer.grabbed) {
      runtime?.pointerUp();
      pointer.grabbed = false;
      pointer.active = false;
      announce("입자를 놓았습니다.");
    } else if (runtime) {
      pointer.grabbed = Boolean(runtime.pointerDown(pointer.x, pointer.y, 58));
      pointer.active = pointer.grabbed;
      announce(pointer.grabbed ? "가까운 입자를 잡았습니다." : "조작점 가까이에 입자가 없습니다.");
    } else {
      announce("현재는 설명용 정적 프리뷰입니다.");
    }
    dirty = true;
  }

  if (event.key === "Escape") {
    releasePointer();
    announce("입자를 놓았습니다.");
  }
});

prefersReducedMotion.addEventListener("change", (event) => {
  if (event.matches && runtime) {
    running = false;
    updatePauseInterface();
    dirty = true;
  }
});

const darkMode = window.matchMedia("(prefers-color-scheme: dark)");
const highContrast = window.matchMedia("(prefers-contrast: more)");
[darkMode, highContrast].forEach((query) => {
  query.addEventListener("change", () => {
    theme = readTheme();
    dirty = true;
  });
});

window.addEventListener("resize", resizeCanvas, { passive: true });
window.addEventListener("beforeunload", () => runtime?.destroy());
document.addEventListener("visibilitychange", () => {
  lastFrameTime = performance.now();
});

buildFallbackIndices();
updateOutputs();
resizeCanvas();
activatePreview("WASM 런타임을 준비하는 동안 정적 프리뷰를 표시합니다");
window.requestAnimationFrame(frame);
initialiseWebAssembly();
