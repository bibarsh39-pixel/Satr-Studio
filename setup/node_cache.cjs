'use strict';
// For larger projects only. Keep normal Node semantics and a new process per run.
try {
  if (!process.env.NODE_DISABLE_COMPILE_CACHE && !process.env.NODE_V8_COVERAGE) {
    const fs = require('node:fs');
    const path = require('node:path');
    const os = require('node:os');
    const root = process.platform === 'win32'
      ? path.join(process.env.LOCALAPPDATA || os.tmpdir(), 'Satr', 'node-cache')
      : path.join(os.homedir(), '.cache', 'satr-studio', 'node');
    // Avoid long-path trouble in Windows' compile-cache directory creation.
    if (process.platform !== 'win32' || root.length < 170) {
      fs.mkdirSync(root, {recursive: true, mode: 0o700});
      const info = fs.lstatSync(root);
      if (info.isDirectory() && !info.isSymbolicLink() &&
          (process.platform === 'win32' || info.uid === process.getuid())) {
        if (process.platform !== 'win32') fs.chmodSync(root, 0o700);
        const cache = require('node:module').enableCompileCache(root);
        if (cache.directory) process.env.NODE_COMPILE_CACHE = cache.directory;
      }
    }
  }
} catch (_) { /* Cache failure must never stop a user's program. */ }
