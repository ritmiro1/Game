(function () {
  'use strict';
  const config = JSON.parse(document.getElementById('playback-config').textContent);
  const video = document.getElementById('player');
  const quality = document.getElementById('quality');
  const automatic = document.getElementById('automatic');
  const status = document.getElementById('status');
  const error = document.getElementById('error');
  let current = 'hd';
  let sequence = 0;
  let waitingTimer = null;
  let samplingTimer = null;
  let previousStats = null;
  let badWindows = 0;

  function clearWaiting() {
    if (waitingTimer !== null) window.clearTimeout(waitingTimer);
    waitingTimer = null;
  }

  function stopSampling() {
    if (samplingTimer !== null) window.clearInterval(samplingTimer);
    samplingTimer = null;
    previousStats = null;
    badWindows = 0;
  }

  function canFallback() {
    return automatic.checked && current === 'hd' && !video.ended && !video.seeking;
  }

  function changeSource(mode, message, resume) {
    const item = config.sources[mode];
    if (!item) return;
    const id = ++sequence;
    const time = Number.isFinite(video.currentTime) ? video.currentTime : 0;
    const shouldResume = resume === undefined ? !video.paused : resume;
    clearWaiting();
    stopSampling();
    video.pause();
    current = mode;
    quality.value = mode;
    error.hidden = true;
    status.textContent = message || item.label;
    video.src = item.src;
    video.addEventListener('loadedmetadata', function () {
      if (id !== sequence) return;
      const duration = Number.isFinite(video.duration) ? video.duration : time;
      video.currentTime = Math.max(0, Math.min(time, duration));
      if (shouldResume) video.play().catch(function () {
        if (id === sequence) status.textContent = 'Копия загружена. Нажмите ▶, чтобы продолжить.';
      });
    }, { once: true });
    video.load();
  }

  function fallback(message) {
    if (canFallback()) changeSource('eco', message);
  }

  function readStats() {
    if (typeof video.getVideoPlaybackQuality === 'function') {
      const result = video.getVideoPlaybackQuality();
      return { total: result.totalVideoFrames, dropped: result.droppedVideoFrames };
    }
    if (typeof video.webkitDecodedFrameCount === 'number') {
      return { total: video.webkitDecodedFrameCount + (video.webkitDroppedFrameCount || 0), dropped: video.webkitDroppedFrameCount || 0 };
    }
    return null;
  }

  function sample() {
    if (document.hidden || video.paused || video.seeking || !canFallback()) {
      previousStats = null;
      badWindows = 0;
      return;
    }
    const stats = readStats();
    if (!stats) return;
    if (previousStats) {
      const frames = stats.total - previousStats.total;
      const drops = stats.dropped - previousStats.dropped;
      if (frames >= 30 && drops >= 0) {
        badWindows = drops / frames > 0.08 ? badWindows + 1 : 0;
        if (badWindows >= 2) {
          fallback('Включён экономный режим: обнаружены пропуски кадров.');
          return;
        }
      } else if (frames < 0 || drops < 0) {
        badWindows = 0;
      }
    }
    previousStats = stats;
  }

  function startSampling() {
    if (samplingTimer === null && !document.hidden && automatic.checked && current === 'hd') samplingTimer = window.setInterval(sample, 2000);
  }

  function waitForBuffer() {
    if (waitingTimer !== null || video.paused || document.hidden || video.readyState >= 3 || !canFallback()) return;
    waitingTimer = window.setTimeout(function () {
      waitingTimer = null;
      if (!video.paused && !document.hidden && video.readyState < 3 && canFallback()) fallback('Включён экономный режим: длительная буферизация.');
    }, 3000);
  }

  quality.addEventListener('change', function () {
    automatic.checked = false;
    changeSource(quality.value, 'Качество выбрано вручную: ' + config.sources[quality.value].label);
  });
  automatic.addEventListener('change', function () {
    stopSampling();
    if (!automatic.checked) clearWaiting();
    else if (!video.paused) startSampling();
  });
  video.addEventListener('playing', function () {
    clearWaiting();
    status.textContent = config.sources[current].label + (automatic.checked && current === 'eco' ? ' · облегчённый режим' : '');
    startSampling();
  });
  video.addEventListener('waiting', waitForBuffer);
  video.addEventListener('stalled', waitForBuffer);
  video.addEventListener('canplay', clearWaiting);
  video.addEventListener('seeking', clearWaiting);
  video.addEventListener('seeked', function () {
    previousStats = null;
    if (!video.paused && video.readyState < 3) waitForBuffer();
  });
  ['pause', 'ended'].forEach(function (event) {
    video.addEventListener(event, function () { clearWaiting(); stopSampling(); });
  });
  video.addEventListener('error', function () {
    if (canFallback()) {
      fallback('Стандартная копия не открылась. Пробую экономную.');
    } else {
      error.textContent = 'Не удалось загрузить видео. Используйте ссылки H.264 ниже или откройте плеер в другом браузере.';
      error.hidden = false;
    }
  });
  document.addEventListener('visibilitychange', function () {
    clearWaiting();
    stopSampling();
    if (!document.hidden && !video.paused) startSampling();
  });

  const connection = navigator.connection || navigator.mozConnection || navigator.webkitConnection;
  if (connection && (connection.saveData || ['slow-2g', '2g'].indexOf(connection.effectiveType) >= 0)) {
    changeSource('eco', 'Экономный режим выбран для медленной сети или режима экономии трафика.', false);
  }
}());
