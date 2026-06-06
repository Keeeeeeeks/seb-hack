/**
 * NdjsonParser.js
 *
 * Accumulates characters from a streaming source, splits on '\n',
 * and JSON.parses each complete line. Malformed lines are silently dropped.
 *
 * Usage:
 *   const parser = new NdjsonParser((obj) => console.log(obj));
 *   parser.push('{"t":1,"roll":0.5}\n{"t":2');
 *   parser.push(',"roll":1.0}\n');
 */
export class NdjsonParser {
  /**
   * @param {(obj: object) => void} onMessage  Called for each valid JSON object.
   */
  constructor(onMessage) {
    if (typeof onMessage !== 'function') {
      throw new TypeError('NdjsonParser: onMessage must be a function');
    }
    this._onMessage = onMessage;
    this._buf = '';
  }

  /**
   * Feed a chunk of text (may contain zero or more newlines).
   * @param {string} chunk
   */
  push(chunk) {
    this._buf += chunk;
    let nl;
    // Process every complete line (terminated by '\n')
    while ((nl = this._buf.indexOf('\n')) !== -1) {
      const line = this._buf.slice(0, nl).trim();
      this._buf = this._buf.slice(nl + 1);
      if (line.length === 0) continue; // skip blank lines
      try {
        const obj = JSON.parse(line);
        this._onMessage(obj);
      } catch (_e) {
        // Silently drop malformed lines — partial frames, corrupt bytes, etc.
      }
    }
  }

  /** Reset internal buffer (call on disconnect). */
  reset() {
    this._buf = '';
  }
}

export default NdjsonParser;
