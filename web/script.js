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
    });

    square.addEventListener('mouseover', () => {
      if (isPainting) {
        toggleSquare(square, img);
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
    sendBtn.addEventListener('click', () => {
      console.log('Grid state (rows x cols):', gridSize, 'x', gridSize);
      console.log(getGridState());
    });
  }
});