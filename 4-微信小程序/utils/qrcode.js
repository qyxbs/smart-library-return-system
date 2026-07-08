/**
 * 小程序 QR 码生成 — 使用微信 canvas 2D API
 *
 * Usage:
 *   const qr = require('../../utils/qrcode');
 *   qr.drawToCanvas(canvasId, content, size, callback)
 *
 * 使用 qrcode-generator 库简化实现，直接通过 canvas 绑定编码数据。
 * 这里提供一个简化版，直接使用微信 wx.createOffscreenCanvas 或
 * 引入第三方 qrcode.js 库。
 *
 * 推荐方案：在页面中使用 <canvas type="2d" id="qr-canvas">
 * 然后调用 drawQR(canvasNode, text, size)
 */

/**
 * 在 canvas 上绘制 QR 码
 * @param {object} canvas - canvas 组件 (通过 SelectorQuery 获取的 node)
 * @param {string} text - QR 码内容
 * @param {number} size - 画布尺寸 (px)
 */
function drawQR(canvas, text, size) {
  if (!canvas) return;

  const ctx = canvas.getContext('2d');
  const dpr = wx.getSystemInfoSync().pixelRatio;
  canvas.width = size * dpr;
  canvas.height = size * dpr;
  ctx.scale(dpr, dpr);

  // 使用内置的 QR 编码算法
  const qr = encodeQR(text);
  const moduleCount = qr.length;
  const moduleSize = size / moduleCount;

  // 白色背景
  ctx.fillStyle = '#FFFFFF';
  ctx.fillRect(0, 0, size, size);

  // 黑色模块
  ctx.fillStyle = '#000000';
  for (let row = 0; row < moduleCount; row++) {
    for (let col = 0; col < moduleCount; col++) {
      if (qr[row][col]) {
        ctx.fillRect(
          col * moduleSize,
          row * moduleSize,
          moduleSize,
          moduleSize
        );
      }
    }
  }
}

/**
 * 简化版 QR 码编码 (支持 alphanumeric + 部分符号)
 * 生成 21x21 (version 1) 到 25x25 (version 2) 的矩阵
 * @param {string} text
 * @returns {boolean[][]}
 */
function encodeQR(text) {
  // 使用字节模式编码
  const data = textToBytes(text);
  const version = data.length <= 17 ? 1 : data.length <= 32 ? 2 : 3;
  const modules = version * 4 + 17;

  // 创建矩阵
  const matrix = Array.from({ length: modules }, () =>
    Array(modules).fill(false));

  // 添加定位图案
  addFinderPatterns(matrix);
  // 添加时序图案
  addTimingPatterns(matrix);
  // 添加对齐图案 (version >= 2)
  if (version >= 2) addAlignmentPattern(matrix, version);

  // 预留格式信息区域 (临时填充)
  // 简化为: 将数据直接放置在数据区域

  // 放置数据
  placeData(matrix, data, version);

  // 应用掩码 (pattern 0: (row+col)%2==0)
  applyMask(matrix, 0);

  // 添加格式信息
  addFormatInfo(matrix, version, 0);

  return matrix;
}

function textToBytes(text) {
  const bytes = [];
  for (let i = 0; i < text.length; i++) {
    const code = text.charCodeAt(i);
    if (code < 128) {
      bytes.push(code);
    } else if (code < 2048) {
      bytes.push(0xC0 | (code >> 6), 0x80 | (code & 0x3F));
    } else {
      bytes.push(0xE0 | (code >> 12), 0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F));
    }
  }
  return bytes;
}

function addFinderPatterns(matrix) {
  const size = matrix.length;
  // 三个角落的定位图案 (7x7)
  const positions = [
    [0, 0], [0, size - 7], [size - 7, 0]
  ];
  for (const [r, c] of positions) {
    for (let i = 0; i < 7; i++) {
      for (let j = 0; j < 7; j++) {
        matrix[r + i][c + j] =
          (i === 0 || i === 6 || j === 0 || j === 6 ||
           (i >= 2 && i <= 4 && j >= 2 && j <= 4));
      }
    }
  }
}

function addTimingPatterns(matrix) {
  const size = matrix.length;
  for (let i = 8; i < size - 8; i++) {
    matrix[6][i] = i % 2 === 0;
    matrix[i][6] = i % 2 === 0;
  }
}

function addAlignmentPattern(matrix, version) {
  const size = matrix.length;
  const centers = getAlignmentCenters(version);
  for (const r of centers) {
    for (const c of centers) {
      // 避免与定位图案重叠
      if ((r < 9 && c < 9) || (r < 9 && c > size - 10) || (r > size - 10 && c < 9)) {
        continue;
      }
      for (let i = -2; i <= 2; i++) {
        for (let j = -2; j <= 2; j++) {
          matrix[r + i][c + j] = (i === -2 || i === 2 || j === -2 || j === 2 ||
                                   (i === 0 && j === 0));
        }
      }
    }
  }
}

function getAlignmentCenters(version) {
  // Version 2: 2 alignment centers at [6, 18]
  if (version === 2) return [6, 18];
  if (version === 3) return [6, 22];
  return [6]; // version 1: no alignment
}

function placeData(matrix, data, version) {
  const size = matrix.length;
  // 简化的数据放置：从右下角开始向上蛇形
  const totalBits = size * size - 8 * 8 * 3 - (version >= 2 ? 25 : 0);
  const dataBits = (data.length + 1) * 8 + 4; // +1 for mode, +4 for terminator

  let bitIndex = 0;
  let col = size - 1;
  let direction = -1; // 向上

  while (col > 0) {
    if (col === 6) col--; // 跳过时序图案列

    for (let row = (direction < 0 ? size - 1 : 0);
         direction < 0 ? row >= 0 : row < size;
         row += direction) {
      for (let c = col; c > col - 2; c--) {
        if (c < 0 || c >= size) continue;
        // 跳过功能图案区域
        if (isFunctional(matrix, row, c)) continue;

        if (bitIndex < dataBits) {
          const byteIdx = Math.floor(bitIndex / 8);
          const bitInByte = 7 - (bitIndex % 8);
          let val;
          if (byteIdx === 0) {
            val = (0x40 >> bitInByte) & 1; // 字节模式指示符: 0100
          } else if (byteIdx <= data.length) {
            val = (data[byteIdx - 1] >> bitInByte) & 1;
          } else {
            val = 0; // terminator + padding
          }
          matrix[row][c] = val === 1;
          bitIndex++;
        }
      }
    }

    col -= 2;
    direction = -direction;
  }
}

function isFunctional(matrix, row, col) {
  const size = matrix.length;
  // 定位图案区域
  if ((row < 9 && col < 9) || (row < 9 && col > size - 9) || (row > size - 9 && col < 9)) {
    return true;
  }
  // 时序图案
  if (row === 6 || col === 6) return true;
  return false;
}

function applyMask(matrix, pattern) {
  const size = matrix.length;
  for (let r = 0; r < size; r++) {
    for (let c = 0; c < size; c++) {
      if (isFunctional(matrix, r, c)) continue;
      let invert = false;
      if (pattern === 0) invert = (r + c) % 2 === 0;
      // other patterns omitted for simplicity
      if (invert) matrix[r][c] = !matrix[r][c];
    }
  }
}

function addFormatInfo(matrix, version, mask) {
  // Simplified — just set dark module
  const size = matrix.length;
  matrix[size - 8][8] = true;
}

module.exports = { drawQR };
