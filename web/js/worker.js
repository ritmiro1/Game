/* РЕЗОНАНС — воркер ИИ (многофайловая версия) */
try {
  const base = new URL('../../shared/', self.location.href);
  importScripts(new URL('content.js', base).href,
    new URL('engine.js', base).href,
    new URL('ai.js', base).href,
    new URL('worker-glue.js', self.location.href).href);
} catch (e) {
  self.postMessage({ id: -1, error: String(e) });
}
