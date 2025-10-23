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
  const espIp = localStorage.getItem('espIp');
  if (!espIp) return null;
  try {
    ws = new WebSocket(`ws://${espIp}:81`);
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
  const sendBtn = document.getElementById('sendChaos');
  if (sendBtn) {
    sendBtn.addEventListener('click', async () => {
      // Ask for ESP IP (use stored value if available)
      let espIp = localStorage.getItem('espIp') || '';
      if (!espIp) {
        espIp = prompt('Enter ESP32 IP (e.g. 192.168.1.123):');
        if (!espIp) return;
        localStorage.setItem('espIp', espIp);
      }

      const state = getGridState();
      console.log('Sending grid to ESP at', espIp);

      const originalText = sendBtn.textContent;
      sendBtn.textContent = 'Sending...';
      sendBtn.disabled = true;

      try {
        const res = await fetch(`http://${espIp}/grid`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ grid: state })
        });
        if (!res.ok) throw new Error('HTTP ' + res.status);
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