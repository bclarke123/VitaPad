#pragma once

// The 3D viewer page, served by viewer.cpp. Plain HTML/JS, edit it like a normal web page.
static const char VIEWER_HTML[] = R"VITAPADHTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VitaPad Viewer</title>
<style>
  :root { --fg: #e6e8ee; --dim: #8a90a0; --accent: #4da3ff; --ok: #3ddc84; --bad: #ff5c5c; --panel: rgba(18, 20, 26, 0.72); }
  html, body { margin: 0; height: 100%; overflow: hidden; background: radial-gradient(ellipse at 50% 35%, #2a2e38 0%, #111318 60%, #08090c 100%); color: var(--fg); font: 13px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif; }
  canvas { display: block; }
  .panel { position: absolute; background: var(--panel); border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; padding: 12px 14px; backdrop-filter: blur(6px); }
  #hud { top: 14px; left: 14px; min-width: 250px; }
  #hud h1 { margin: 0 0 8px; font-size: 15px; font-weight: 600; letter-spacing: 0.02em; }
  #status { display: inline-flex; align-items: center; gap: 6px; font-weight: 600; }
  #status::before { content: ""; width: 9px; height: 9px; border-radius: 50%; background: var(--bad); box-shadow: 0 0 8px var(--bad); }
  #status.on::before { background: var(--ok); box-shadow: 0 0 8px var(--ok); }
  .row { display: flex; justify-content: space-between; gap: 16px; font-variant-numeric: tabular-nums; }
  .row span:first-child { color: var(--dim); }
  .bar { height: 6px; background: rgba(255,255,255,0.1); border-radius: 3px; overflow: hidden; margin: 2px 0 6px; }
  .bar div { height: 100%; background: var(--ok); width: 0; transition: width 0.3s; }
  #pressed { min-height: 20px; margin-top: 6px; display: flex; flex-wrap: wrap; gap: 4px; }
  #pressed b { background: var(--accent); color: #041222; border-radius: 4px; padding: 1px 6px; font-size: 11px; }
  #controls { top: 14px; right: 14px; display: flex; flex-direction: column; gap: 8px; }
  button { background: #2b3040; color: var(--fg); border: 1px solid rgba(255,255,255,0.12); border-radius: 7px; padding: 7px 12px; font: inherit; cursor: pointer; }
  button:hover { background: #363c50; }
  label { display: flex; align-items: center; gap: 6px; cursor: pointer; user-select: none; }
  #help { bottom: 14px; left: 50%; transform: translateX(-50%); color: var(--dim); font-size: 12px; padding: 7px 12px; text-align: center; }
  #error { top: 50%; left: 50%; transform: translate(-50%, -50%); display: none; max-width: 420px; text-align: center; }
</style>
<script type="importmap">
{ "imports": {
  "three": "https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js",
  "three/addons/": "https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/"
} }
</script>
</head>
<body>
<div id="hud" class="panel">
  <h1>VitaPad Viewer</h1>
  <div id="status">Waiting for the client...</div>
  <div class="row"><span>Packets</span><span id="rate">0 /s</span></div>
  <div class="row"><span>Battery</span><span id="battery">-</span></div>
  <div class="bar"><div id="batterybar"></div></div>
  <div class="row"><span>Left stick</span><span id="lstick">-</span></div>
  <div class="row"><span>Right stick</span><span id="rstick">-</span></div>
  <div class="row"><span>Accel (G)</span><span id="accel">-</span></div>
  <div class="row"><span>Gyro (deg/s)</span><span id="gyro">-</span></div>
  <div class="row"><span>Front touch</span><span id="ftouch">-</span></div>
  <div class="row"><span>Rear touch</span><span id="rtouch">-</span></div>
  <div id="pressed"></div>
</div>
<div id="controls" class="panel">
  <button id="recenter">Recenter (R)</button>
  <button id="flip">Show back (B)</button>
  <label><input type="checkbox" id="follow" checked> Follow motion</label>
</div>
<div id="help" class="panel">Drag to orbit - scroll to zoom - rear touches also appear as rings on the screen</div>
<div id="error" class="panel">Couldn't load the 3D engine. The viewer downloads three.js from cdn.jsdelivr.net, check your internet connection and reload.</div>

<script type="module">
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';

// ---------- Scene ----------
const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.shadowMap.enabled = true;
document.body.prepend(renderer.domElement);

const scene = new THREE.Scene();
scene.environment = new THREE.PMREMGenerator(renderer).fromScene(new RoomEnvironment(), 0.04).texture;

const camera = new THREE.PerspectiveCamera(35, innerWidth / innerHeight, 1, 2000);
camera.position.set(0, 130, 300);
const orbit = new OrbitControls(camera, renderer.domElement);
orbit.enableDamping = true;
orbit.minDistance = 150;
orbit.maxDistance = 700;

const key = new THREE.DirectionalLight(0xffffff, 1.6);
key.position.set(120, 200, 250);
key.castShadow = true;
scene.add(key, new THREE.AmbientLight(0xffffff, 0.25));

const floor = new THREE.Mesh(new THREE.CircleGeometry(260, 64), new THREE.ShadowMaterial({ opacity: 0.25 }));
floor.rotation.x = -Math.PI / 2;
floor.position.y = -110;
floor.receiveShadow = true;
scene.add(floor);

// ---------- Vita model (units: mm, Vita axes: X right, Y top of the screen, Z out of the screen) ----------
const W = 182, H = 84, DEPTH = 14, BEVEL = 2.4;
const FRONT = DEPTH / 2 + BEVEL, BACK = -FRONT;
const vita = new THREE.Group();
scene.add(vita);

function roundedRect(w, h, r) {
  const s = new THREE.Shape(), x = w / 2, y = h / 2;
  s.moveTo(-x + r, -y); s.lineTo(x - r, -y); s.quadraticCurveTo(x, -y, x, -y + r);
  s.lineTo(x, y - r); s.quadraticCurveTo(x, y, x - r, y); s.lineTo(-x + r, y);
  s.quadraticCurveTo(-x, y, -x, y - r); s.lineTo(-x, -y + r); s.quadraticCurveTo(-x, -y, -x + r, -y);
  return s;
}

const bodyGeo = new THREE.ExtrudeGeometry(roundedRect(W, H, 36), { depth: DEPTH, bevelThickness: BEVEL, bevelSize: 2.4, bevelSegments: 6, curveSegments: 48 });
bodyGeo.translate(0, 0, -DEPTH / 2);
const body = new THREE.Mesh(bodyGeo, new THREE.MeshPhysicalMaterial({ color: 0x15161a, roughness: 0.32, metalness: 0.1, clearcoat: 1, clearcoatRoughness: 0.15 }));
body.castShadow = true;
vita.add(body);

// Canvas textures for the screen and the rear touchpad
function canvasPlane(w, h, px, py, z, flip) {
  const canvas = document.createElement('canvas');
  canvas.width = px; canvas.height = py;
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 8;
  const mesh = new THREE.Mesh(new THREE.PlaneGeometry(w, h), new THREE.MeshBasicMaterial({ map: texture, toneMapped: false }));
  mesh.position.z = z;
  if (flip) mesh.rotation.y = Math.PI;
  vita.add(mesh);
  return { ctx: canvas.getContext('2d'), texture, px, py };
}
const screen = canvasPlane(110, 62, 960, 544, FRONT + 0.05, false);
const rearPad = canvasPlane(118, 52, 944, 416, BACK - 0.05, true);

// Glass bezel around the screen
const bezel = new THREE.Mesh(new THREE.ShapeGeometry(roundedRect(118, 68, 3)), new THREE.MeshPhysicalMaterial({ color: 0x050506, roughness: 0.05, clearcoat: 1 }));
bezel.position.z = FRONT + 0.02;
vita.add(bezel);

// Buttons
const pressables = []; // { mesh, mask, axis, depth, base, material, glow }

function glyphTexture(draw) {
  const c = document.createElement('canvas'); c.width = c.height = 128;
  const g = c.getContext('2d');
  g.fillStyle = '#1b1c21'; g.fillRect(0, 0, 128, 128);
  draw(g);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

function addPressable(mesh, mask, glow, axis = 'z', depth = 1.2) {
  mesh.castShadow = true;
  vita.add(mesh);
  const materials = [];
  mesh.traverse(o => { if (o.material && o.material.emissive) materials.push(o.material); });
  pressables.push({ mesh, mask, axis, depth, base: mesh.position[axis], materials, glow: new THREE.Color(glow) });
}

const plastic = () => new THREE.MeshPhysicalMaterial({ color: 0x1d1e23, roughness: 0.4, clearcoat: 0.6 });

function faceButton(x, y, mask, color, draw) {
  const mesh = new THREE.Mesh(new THREE.CylinderGeometry(4.3, 4.3, 2.4, 40), plastic());
  mesh.rotation.x = Math.PI / 2;
  // The symbol sits on its own disc so the texture is upright
  const glyph = new THREE.Mesh(new THREE.CircleGeometry(4.3, 40), new THREE.MeshPhysicalMaterial({ map: glyphTexture(draw), roughness: 0.35, clearcoat: 0.8 }));
  glyph.rotation.x = -Math.PI / 2;
  glyph.position.y = 1.21;
  mesh.add(glyph);
  mesh.position.set(x, y, FRONT + 0.6);
  addPressable(mesh, mask, color);
}

const stroke = (color, fn) => g => { g.strokeStyle = color; g.lineWidth = 11; g.lineCap = 'round'; g.lineJoin = 'round'; g.beginPath(); fn(g); g.stroke(); };
const FB = { x: 71, y: 13, d: 9.5 };
faceButton(FB.x, FB.y + FB.d, 0x1000, '#3ee6a0', stroke('#3ee6a0', g => { g.moveTo(64, 30); g.lineTo(98, 90); g.lineTo(30, 90); g.closePath(); }));
faceButton(FB.x + FB.d, FB.y, 0x2000, '#ff5f6d', stroke('#ff5f6d', g => g.arc(64, 64, 32, 0, Math.PI * 2)));
faceButton(FB.x, FB.y - FB.d, 0x4000, '#6aa8ff', stroke('#6aa8ff', g => { g.moveTo(34, 34); g.lineTo(94, 94); g.moveTo(94, 34); g.lineTo(34, 94); }));
faceButton(FB.x - FB.d, FB.y, 0x8000, '#ff8fd0', stroke('#ff8fd0', g => g.rect(34, 34, 60, 60)));

// D-pad
const DP = { x: -71, y: 13 };
const dpadCenter = new THREE.Mesh(new THREE.BoxGeometry(6.4, 6.4, 1.8), plastic());
dpadCenter.position.set(DP.x, DP.y, FRONT + 0.5);
vita.add(dpadCenter);
[[0, 1, 0x0010], [1, 0, 0x0020], [0, -1, 0x0040], [-1, 0, 0x0080]].forEach(([dx, dy, mask]) => {
  const arm = new THREE.Mesh(new THREE.BoxGeometry(dx ? 7.5 : 6.4, dy ? 7.5 : 6.4, 1.8), plastic());
  arm.position.set(DP.x + dx * 6.9, DP.y + dy * 6.9, FRONT + 0.5);
  addPressable(arm, mask, '#4da3ff');
});

// Analog sticks
function stick(x, y) {
  const well = new THREE.Mesh(new THREE.CylinderGeometry(7, 7, 0.6, 40), new THREE.MeshStandardMaterial({ color: 0x0b0b0d, roughness: 0.9 }));
  well.rotation.x = Math.PI / 2;
  well.position.set(x, y, FRONT + 0.1);
  vita.add(well);
  const cap = new THREE.Group();
  const knob = new THREE.Mesh(new THREE.CylinderGeometry(5.6, 5.8, 3.2, 40), new THREE.MeshPhysicalMaterial({ color: 0x232429, roughness: 0.75, clearcoat: 0.2 }));
  knob.rotation.x = Math.PI / 2;
  knob.castShadow = true;
  const dot = new THREE.Mesh(new THREE.CircleGeometry(1.2, 20), new THREE.MeshBasicMaterial({ color: 0x4da3ff }));
  dot.position.z = 1.65;
  cap.add(knob, dot);
  cap.position.set(x, y, FRONT + 1.7);
  vita.add(cap);
  return { cap, x, y, dot };
}
const lStick = stick(-64, -21), rStick = stick(64, -21);

// Start / Select / PS
function pill(x, y, mask) {
  const mesh = new THREE.Mesh(new THREE.CapsuleGeometry(1.3, 3.5, 6, 16), plastic());
  mesh.rotation.z = Math.PI / 2;
  mesh.position.set(x, y, FRONT + 0.4);
  addPressable(mesh, mask, '#ffd166', 'z', 0.8);
}
pill(65, -33.5, 0x0001); // Select
pill(76, -33.5, 0x0008); // Start
const ps = new THREE.Mesh(new THREE.CylinderGeometry(2.8, 2.8, 1.2, 32), new THREE.MeshPhysicalMaterial({ color: 0x1d1e23, roughness: 0.3, emissive: 0x1a3a66, emissiveIntensity: 0.6 }));
ps.rotation.x = Math.PI / 2;
ps.position.set(-73, -33, FRONT + 0.3);
vita.add(ps);

// Shoulder buttons
function shoulder(x, mask) {
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(24, 3, 9), plastic());
  mesh.position.set(x, H / 2 + 1.2, 0);
  addPressable(mesh, mask, '#4da3ff', 'y', 1.2);
}
shoulder(-60, 0x0100);
shoulder(60, 0x0200);

// ---------- Drawing touches ----------
function drawScreen(front, rear) {
  const { ctx: g, px, py } = screen;
  const grad = g.createLinearGradient(0, 0, px, py);
  grad.addColorStop(0, '#0b1a33'); grad.addColorStop(1, '#050a14');
  g.fillStyle = grad; g.fillRect(0, 0, px, py);
  g.fillStyle = 'rgba(255,255,255,0.06)';
  g.font = '600 64px system-ui, sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle';
  g.fillText('VitaPad', px / 2, py / 2);
  g.strokeStyle = 'rgba(255,255,255,0.05)'; g.lineWidth = 2;
  g.beginPath(); g.moveTo(px / 2, 0); g.lineTo(px / 2, py); g.moveTo(0, py / 2); g.lineTo(px, py / 2); g.stroke();
  for (const [x, y] of rear) { // rear touches seen through the Vita
    g.strokeStyle = 'rgba(255, 143, 208, 0.9)'; g.lineWidth = 6;
    g.beginPath(); g.arc(x / 1920 * px, y / 1088 * py, 34, 0, Math.PI * 2); g.stroke();
  }
  for (const [x, y] of front) {
    const cx = x / 1920 * px, cy = y / 1088 * py;
    const glow = g.createRadialGradient(cx, cy, 0, cx, cy, 60);
    glow.addColorStop(0, 'rgba(120, 190, 255, 0.95)'); glow.addColorStop(1, 'rgba(120, 190, 255, 0)');
    g.fillStyle = glow; g.beginPath(); g.arc(cx, cy, 60, 0, Math.PI * 2); g.fill();
    g.fillStyle = '#fff'; g.beginPath(); g.arc(cx, cy, 12, 0, Math.PI * 2); g.fill();
  }
  screen.texture.needsUpdate = true;
}

function drawRear(rear) {
  const { ctx: g, px, py } = rearPad;
  g.fillStyle = '#1c1d23'; g.fillRect(0, 0, px, py);
  g.strokeStyle = 'rgba(255,255,255,0.14)'; g.lineWidth = 6; g.strokeRect(3, 3, px - 6, py - 6);
  g.fillStyle = 'rgba(255,255,255,0.07)';
  const glyphs = ['△', '○', '✕', '□'];
  g.font = '28px system-ui, sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle';
  for (let row = 0; row < 8; row++) for (let col = 0; col < 18; col++)
    g.fillText(glyphs[(row + col) % 4], 26 + col * 52 + (row % 2) * 26, 26 + row * 52);
  for (const [x, y] of rear) {
    // Rear coordinates are as seen from the front, the pad is seen from behind: mirror X
    const cx = px - x / 1920 * px, cy = y / 1088 * py;
    const glow = g.createRadialGradient(cx, cy, 0, cx, cy, 55);
    glow.addColorStop(0, 'rgba(255, 143, 208, 0.95)'); glow.addColorStop(1, 'rgba(255, 143, 208, 0)');
    g.fillStyle = glow; g.beginPath(); g.arc(cx, cy, 55, 0, Math.PI * 2); g.fill();
  }
  rearPad.texture.needsUpdate = true;
}
drawScreen([], []);
drawRear([]);

// ---------- Orientation (Mahony filter: gyro integration corrected by gravity) ----------
const orientation = new THREE.Quaternion();
let lastT = null, samples = 0, recentered = false;

function fuse(accel, gyro, t) {
  const dt = lastT === null ? 0 : ((t - lastT) >>> 0) / 1e6;
  lastT = t;
  if (dt <= 0 || dt > 0.25) return;
  samples++;
  const w = new THREE.Vector3(...gyro); // rad/s
  const up = new THREE.Vector3(-accel[0], -accel[1], -accel[2]); // the Vita reports gravity, we want "up"
  const n = up.length();
  if (n > 0.6 && n < 1.4) {
    up.divideScalar(n);
    const estimatedUp = new THREE.Vector3(0, 1, 0).applyQuaternion(orientation.clone().invert());
    const gain = samples < 60 ? 8 : 1; // converge fast at start
    w.addScaledVector(new THREE.Vector3().crossVectors(up, estimatedUp), gain);
  }
  const angle = w.length() * dt;
  if (angle > 0) orientation.multiply(new THREE.Quaternion().setFromAxisAngle(w.normalize(), angle)).normalize();
  if (!recentered && samples > 90) { recenter(); recentered = true; }
}

// Removes the yaw (drifts without a compass) so the Vita faces the camera again
function recenter() {
  const screenNormal = new THREE.Vector3(0, 0, 1).applyQuaternion(orientation);
  const top = new THREE.Vector3(0, 1, 0).applyQuaternion(orientation);
  // Upright Vita: use where the screen faces. Flat Vita: use where its top points (away from you)
  const useNormal = Math.hypot(screenNormal.x, screenNormal.z) > 0.5;
  const v = useNormal ? screenNormal : top;
  const yaw = Math.atan2(v.x, v.z) - (useNormal ? 0 : Math.PI);
  orientation.premultiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), -yaw)).normalize();
}

// ---------- UI ----------
const $ = id => document.getElementById(id);
let showBack = false;
$('recenter').onclick = recenter;
$('flip').onclick = () => { showBack = !showBack; $('flip').textContent = showBack ? 'Show front (B)' : 'Show back (B)'; };
addEventListener('keydown', e => {
  if (e.key === 'r' || e.key === 'R') recenter();
  if (e.key === 'b' || e.key === 'B') $('flip').click();
});
addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});

const BUTTON_NAMES = [[0x0010, 'UP'], [0x0040, 'DOWN'], [0x0080, 'LEFT'], [0x0020, 'RIGHT'], [0x4000, 'CROSS'], [0x2000, 'CIRCLE'],
  [0x8000, 'SQUARE'], [0x1000, 'TRIANGLE'], [0x0100, 'L'], [0x0200, 'R'], [0x0008, 'START'], [0x0001, 'SELECT']];
const fmt = (v, d) => v.map(x => x.toFixed(d).padStart(d + 4)).join(' ');

let state = null;
function onState(s) {
  const wasConnected = state && state.connected;
  state = s;
  $('status').textContent = s.connected ? 'Vita connected' : 'Waiting for the Vita...';
  $('status').className = s.connected ? 'on' : '';
  $('rate').textContent = s.rate + ' /s';
  if (!s.connected) { lastT = null; return; }
  if (!wasConnected) { samples = 0; recentered = false; }
  $('battery').textContent = s.battery + '%';
  $('batterybar').style.width = s.battery + '%';
  $('lstick').textContent = s.lx + ', ' + s.ly;
  $('rstick').textContent = s.rx + ', ' + s.ry;
  $('accel').textContent = fmt(s.accel, 2);
  $('gyro').textContent = fmt(s.gyro.map(g => g * 180 / Math.PI), 0);
  $('ftouch').textContent = s.front.map(p => p.join(',')).join('  ') || '-';
  $('rtouch').textContent = s.rear.map(p => p.join(',')).join('  ') || '-';
  $('pressed').innerHTML = BUTTON_NAMES.filter(([m]) => s.buttons & m).map(([, n]) => '<b>' + n + '</b>').join('');
  fuse(s.accel, s.gyro, s.t);
  drawScreen(s.front, s.rear);
  drawRear(s.rear);
}

function connect() {
  const events = new EventSource('/events');
  events.onmessage = e => onState(JSON.parse(e.data));
  events.onerror = () => {
    $('status').textContent = 'VitaPad client closed';
    $('status').className = '';
  };
}
connect();

// ---------- Render loop ----------
const flipQuat = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), Math.PI);
const target = new THREE.Quaternion();
const black = new THREE.Color(0);
renderer.setAnimationLoop(() => {
  const buttons = state && state.connected ? state.buttons : 0;
  for (const p of pressables) {
    const on = (buttons & p.mask) !== 0;
    p.mesh.position[p.axis] += ((on ? p.base - p.depth : p.base) - p.mesh.position[p.axis]) * 0.5;
    for (const m of p.materials) { m.emissive.copy(on ? p.glow : black); m.emissiveIntensity = on ? 0.9 : 0; }
  }
  if (state && state.connected) {
    for (const [s, x, y] of [[lStick, state.lx, state.ly], [rStick, state.rx, state.ry]]) {
      const dx = (x - 128) / 128, dy = -(y - 128) / 128;
      s.cap.position.x = s.x + dx * 2.2;
      s.cap.position.y = s.y + dy * 2.2;
      s.cap.rotation.set(-dy * 0.35, dx * 0.35, 0);
      s.dot.material.color.set(Math.hypot(dx, dy) > 0.2 ? 0x7cf0ff : 0x4da3ff);
    }
  }
  target.copy($('follow').checked ? orientation : new THREE.Quaternion());
  if (showBack) target.premultiply(flipQuat);
  vita.quaternion.slerp(target, 0.35);
  orbit.update();
  renderer.render(scene, camera);
});
</script>
<script>
  // Shown if the module above fails to load (no internet for the CDN)
  addEventListener('error', e => { if (e.target && e.target.tagName === 'SCRIPT') document.getElementById('error').style.display = 'block'; }, true);
  setTimeout(() => { if (!document.querySelector('canvas')) document.getElementById('error').style.display = 'block'; }, 6000);
</script>
</body>
</html>
)VITAPADHTML";
