const grid = document.getElementById('grid');
const gridSize = 16; // 16x16 grid
let isPainting = false;

// Color button logic
let currentColor = '#FFFFFF'; // Default to white
const colorButtons = document.querySelectorAll('.color-btn');
colorButtons.forEach(btn => {
  btn.addEventListener('click', () => {
    currentColor = btn.getAttribute('data-color');
    colorButtons.forEach(b => b.classList.remove('selected'));
    btn.classList.add('selected');
    // Selecting a color switches back to draw mode
    currentMode = 'draw';
    const eraseBtn = document.getElementById('eraseBtn');
    if (eraseBtn) eraseBtn.classList.remove('selected');
  });
});
// Select the white button by default
const whiteBtn = Array.from(colorButtons).find(btn => btn.getAttribute('data-color') === '#FFFFFF');
if (whiteBtn) {
  whiteBtn.classList.add('selected');
}

// Mode buttons
let currentMode = 'draw'; // or 'erase'
const eraseBtn = document.getElementById('eraseBtn');
if (eraseBtn) {
  eraseBtn.addEventListener('click', () => {
    if (currentMode === 'erase') {
      currentMode = 'draw';
      eraseBtn.classList.remove('selected');
    } else {
      currentMode = 'erase';
      eraseBtn.classList.add('selected');
      // clear color selection when entering erase
      colorButtons.forEach(b => b.classList.remove('selected'));
    }
  });
}

function toggleSquare(square, img) {
  if (currentMode === 'draw') {
    // Draw mode: show and tint
    img.style.display = 'block';
    square.classList.add('has-image');
    square.style.backgroundColor = currentColor;
    img.style.mixBlendMode = 'multiply';
    img.style.filter = 'none';
  } else if (currentMode === 'erase') {
    // Erase mode: hide image and clear tint
    img.style.display = 'none';
    square.classList.remove('has-image');
    square.style.backgroundColor = '';
    img.style.mixBlendMode = '';
  }
}

for (let row = 0; row < gridSize; row++) {
  for (let col = 0; col < gridSize; col++) {
    const square = document.createElement('div');
    square.className = 'square';
    square.dataset.r = row;
    square.dataset.c = col;

    // Add an img element for toggling
    const img = document.createElement('img');
    img.src = 'brick2x2.jpeg'; // Local brick image
    img.style.display = 'none';
    img.style.width = '100%';
    img.style.height = '100%';
    img.style.pointerEvents = 'none';
    img.style.position = 'absolute';
    img.style.left = '0';
    img.style.top = '0';
    square.appendChild(img);

    square.addEventListener('mousedown', (e) => {
      e.preventDefault();
      isPainting = true;
      toggleSquare(square, img);
      // send cell update via websocket if available
      const r = Number(square.dataset.r);
      const c = Number(square.dataset.c);
      const color = square.classList.contains('has-image') ? (square.style.backgroundColor || null) : null;
      sendCellUpdate(r, c, color);
    });

    square.addEventListener('mouseover', () => {
      if (isPainting) {
        toggleSquare(square, img);
        const r = Number(square.dataset.r);
        const c = Number(square.dataset.c);
        const color = square.classList.contains('has-image') ? (square.style.backgroundColor || null) : null;
        sendCellUpdate(r, c, color);
      }
    });

    // Prevent unwanted text selection while painting
    square.addEventListener('dragstart', (e) => e.preventDefault());

    grid.appendChild(square);
  }
}

document.addEventListener('mouseup', () => {
  isPainting = false;
});

// WebSocket helper
let ws = null;
function ensureWebSocket() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return ws;
  const ipInput = document.getElementById('espIpInput');
  let espIp = null;
  if (ipInput && ipInput.value && ipInput.value.trim()) espIp = ipInput.value.trim();
  if (!espIp) return null;
  try {
    // if user provided a port (e.g. 10.0.0.5:8080), strip it for websocket host
    const host = espIp.split(':')[0];
    ws = new WebSocket(`ws://${host}:81`);
    ws.addEventListener('open', () => console.log('WebSocket open'));
    ws.addEventListener('close', () => console.log('WebSocket closed'));
    ws.addEventListener('error', (e) => console.error('WebSocket error', e));
  } catch (e) {
    console.error('Failed to create WebSocket', e);
    ws = null;
  }
  return ws;
}

function sendCellUpdate(r, c, color) {
  const socket = ensureWebSocket();
  if (!socket || socket.readyState !== WebSocket.OPEN) return;
  const msg = JSON.stringify({ type: 'cell', r, c, color });
  socket.send(msg);
}

// Return a 2D array representing the grid state: null for empty, or color string for filled
function getGridState() {
  const squares = Array.from(grid.querySelectorAll('.square'));
  const state = [];
  for (let r = 0; r < gridSize; r++) {
    const row = [];
    for (let c = 0; c < gridSize; c++) {
      const idx = r * gridSize + c;
      const sq = squares[idx];
      if (!sq) {
        row.push(null);
        continue;
      }
      if (sq.classList.contains('has-image')) {
        // Use the inline backgroundColor if present; normalize empty string to null
        const col = sq.style.backgroundColor || null;
        row.push(col);
      } else {
        row.push(null);
      }
    }
    state.push(row);
  }
  return state;
}

// Wire the Send chaos button to print the grid state after DOM is ready
document.addEventListener('DOMContentLoaded', () => {
  // ESP IP input and Test connection
  const espIpInput = document.getElementById('espIpInput');
  const testBtn = document.getElementById('testEspBtn');
  const espStatus = document.getElementById('espStatus');
  if (espIpInput) {
    const saved = localStorage.getItem('espIp');
    if (saved) espIpInput.value = saved;
    espIpInput.addEventListener('change', () => {
      localStorage.setItem('espIp', espIpInput.value.trim());
    });
  }

  async function checkEspReachable(ip) {
    if (!ip) return false;
    try {
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 2000);
      // Probe the simpler /status endpoint (GET) which we add to the ESP firmware.
      let res = null;
      try {
        if (ip.includes(':')) {
          // user supplied host:port
          res = await fetch(`http://${ip}/status`, { method: 'GET', signal: controller.signal });
        } else {
          // try default port (80) first, then fallback to 8080
          try {
            res = await fetch(`http://${ip}/status`, { method: 'GET', signal: controller.signal });
          } catch (e) {
            console.debug('Primary /status failed, trying :8080', e);
            res = await fetch(`http://${ip}:8080/status`, { method: 'GET', signal: controller.signal });
          }
        }
      } finally {
        clearTimeout(timeout);
      }
      if (!res) return false;
      if (!res.ok) return false;
      // Optionally validate JSON body
      try {
        const j = await res.json();
        return j && (j.status === 'ok' || j.status === 'offline' || j.status === 'ok');
      } catch (e) {
        return true; // reachable even if JSON parsing failed
      }
    } catch (e) {
      console.debug('checkEspReachable error', e);
      return false;
    }
  }

  if (testBtn) {
    testBtn.addEventListener('click', async () => {
      const ip = (espIpInput && espIpInput.value.trim()) || localStorage.getItem('espIp') || '';
      if (!ip) { alert('Enter an ESP IP first'); return; }
      testBtn.textContent = 'Testing...'; testBtn.disabled = true;
      const ok = await checkEspReachable(ip);
      testBtn.disabled = false; testBtn.textContent = 'Test connection';
      if (espStatus) {
        if (ok) { espStatus.textContent = `Reachable (${ip})`; espStatus.style.color = 'green'; ensureWebSocket(); localStorage.setItem('espIp', ip); }
        else { espStatus.textContent = `Unreachable (${ip})`; espStatus.style.color = 'crimson'; }
      }
    });
  }

  const sendBtn = document.getElementById('sendChaos');
  if (sendBtn) {
    sendBtn.addEventListener('click', async () => {
      // Prefer IP from input, otherwise stored value or prompt
      let espIp = '';
      if (espIpInput && espIpInput.value.trim()) espIp = espIpInput.value.trim();
      if (!espIp) espIp = localStorage.getItem('espIp') || '';
      if (!espIp) {
        espIp = prompt('Enter ESP32 IP (e.g. 192.168.1.123):');
        if (!espIp) return;
        localStorage.setItem('espIp', espIp);
      }

      // quick reachability check
      const reachable = await checkEspReachable(espIp);
      if (!reachable) { alert(`Cannot reach ESP at ${espIp}. Check network and IP.`); return; }

      const state = getGridState();
      console.log('Sending grid to ESP at', espIp);

      const originalText = sendBtn.textContent;
      sendBtn.textContent = 'Sending...';
      sendBtn.disabled = true;

      try {
        // Try default (may include port if user typed ip:port)
        let postUrls = [];
        if (espIp.includes(':')) postUrls.push(`http://${espIp}/grid`);
        else {
          postUrls.push(`http://${espIp}/grid`);
          postUrls.push(`http://${espIp}:8080/grid`);
        }

        let res = null;
        let lastErr = null;
        for (const url of postUrls) {
          try {
            res = await fetch(url, {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ grid: state })
            });
            if (res.ok) break; // success
            lastErr = new Error('HTTP ' + res.status);
          } catch (e) {
            lastErr = e;
            res = null;
          }
        }

        if (!res || !res.ok) throw lastErr || new Error('Failed to POST to any URL');
        const json = await res.json();
        console.log('ESP response', json);
        sendBtn.textContent = 'Sent ✓';
        setTimeout(() => { sendBtn.textContent = originalText; sendBtn.disabled = false; }, 1200);
      } catch (err) {
        console.error('Failed to send grid to ESP:', err);
        sendBtn.textContent = 'Failed';
        setTimeout(() => { sendBtn.textContent = originalText; sendBtn.disabled = false; }, 1500);
      }
    });
  }
});