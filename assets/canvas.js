// Canvas fingerprinting protection, injected into every frame before page scripts.
// MODE 1 = block (readbacks return blank data, like Tor), MODE 2 = noise (tiny per-site, per-session changes).
(function (SEED, MODE) {
  'use strict';

  const hash = (s) => {
    let h = 2166136261 >>> 0;
    for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619) >>> 0;
    return h;
  };
  const seed = (SEED ^ hash(String(location.origin))) >>> 0;   // differs per site, stable per session

  const rng = (a) => () => {
    a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };

  // flip the low bit of one colour channel in roughly 1 of 13 pixels
  const perturb = (data) => {
    const r = rng(seed);
    for (let i = ((r() * 8) | 0) * 4; i < data.length; i += 4 * (1 + ((r() * 24) | 0))) {
      data[i + ((r() * 3) | 0)] ^= 1;
    }
  };
  const scrub = (data) => (MODE === 1 ? data.fill(0) : perturb(data));

  // make our wrappers pass for native functions
  const natives = new WeakMap();
  const fnToString = Function.prototype.toString;
  const patchedToString = {
    toString() { return natives.has(this) ? natives.get(this) : fnToString.call(this); },
  }.toString;
  natives.set(patchedToString, fnToString.call(fnToString));
  Object.defineProperty(Function.prototype, 'toString',
    { value: patchedToString, writable: true, enumerable: false, configurable: true });

  const wrap = (proto, name, impl) => {
    const native = proto && proto[name];
    if (typeof native !== 'function') return;
    const fn = { [name]() { return impl(native, this, arguments); } }[name];
    Object.defineProperty(fn, 'length', { value: native.length });
    natives.set(fn, fnToString.call(native));
    Object.defineProperty(proto, name, { value: fn, writable: true, enumerable: true, configurable: true });
  };

  // a blank (block) or noised (noise) copy of a canvas; reads of it go through the wrapped getImageData
  const snapshot = (src) => {
    const w = src.width, h = src.height;
    if (!w || !h) return src;
    const c = (typeof HTMLCanvasElement !== 'undefined' && src instanceof HTMLCanvasElement)
      ? document.createElement('canvas') : new OffscreenCanvas(w, h);
    c.width = w;
    c.height = h;
    if (MODE === 2) {
      const ctx = c.getContext('2d');
      ctx.drawImage(src, 0, 0);                    // tainted canvases throw here, as natively
      ctx.putImageData(ctx.getImageData(0, 0, w, h), 0, 0);   // getImageData is the noised wrapper
    }
    return c;
  };

  const image = (native, self, args) => {
    const img = native.apply(self, args);
    if (img && img.data instanceof Uint8ClampedArray) scrub(img.data);
    return img;
  };
  const viaSnapshot = (native, self, args) => native.apply(snapshot(self), args);

  if (typeof CanvasRenderingContext2D !== 'undefined')
    wrap(CanvasRenderingContext2D.prototype, 'getImageData', image);
  if (typeof OffscreenCanvasRenderingContext2D !== 'undefined')
    wrap(OffscreenCanvasRenderingContext2D.prototype, 'getImageData', image);

  if (typeof HTMLCanvasElement !== 'undefined') {
    wrap(HTMLCanvasElement.prototype, 'toDataURL', viaSnapshot);
    wrap(HTMLCanvasElement.prototype, 'toBlob', viaSnapshot);
  }
  if (typeof OffscreenCanvas !== 'undefined')
    wrap(OffscreenCanvas.prototype, 'convertToBlob', viaSnapshot);

  for (const name of ['WebGLRenderingContext', 'WebGL2RenderingContext']) {
    if (typeof self[name] === 'undefined') continue;
    wrap(self[name].prototype, 'readPixels', (native, self2, args) => {
      const r = native.apply(self2, args);
      const px = args[6];
      if (px && px.BYTES_PER_ELEMENT === 1) scrub(px);
      return r;
    });
  }
})
