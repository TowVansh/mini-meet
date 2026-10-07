// Headless call participant for automated measurements. Uses Chromium's
// synthetic camera and microphone, so no hardware is needed.
//
//   node bench/bot.js --url http://localhost:8443 --room test --name botA --adapt 1 --run myrun --secs 60

const puppeteer = require('puppeteer');

const CHROME_ARGS = [
  '--use-fake-ui-for-media-stream',
  '--use-fake-device-for-media-stream',
  '--autoplay-policy=no-user-gesture-required',
  // Show real host IPs instead of mDNS .local names, which do not resolve in WSL.
  '--disable-features=WebRtcHideLocalIpsWithMdns',
  '--no-sandbox',
];

async function launchBrowser() {
  return puppeteer.launch({ headless: true, args: CHROME_ARGS });
}

async function openBot(browser, { url, room, name, adapt, run }) {
  const page = await browser.newPage();
  page.on('console', (msg) => {
    if (msg.type() === 'error' || msg.type() === 'warn') console.log(`[${name}] ${msg.text()}`);
  });
  const q = new URLSearchParams({ room, name, auto: '1', adapt: String(adapt) });
  if (run) q.set('run', run);
  await page.goto(`${url}/?${q}`, { waitUntil: 'load' });
  return page;
}

async function closeBot(page) {
  // Leave cleanly so the last stats batch is flushed.
  await page.evaluate(() => document.querySelector('#btn-leave')?.click()).catch(() => {});
  await new Promise((r) => setTimeout(r, 500));
  await page.close().catch(() => {});
}

module.exports = { launchBrowser, openBot, closeBot };

if (require.main === module) {
  const arg = (k, d) => {
    const i = process.argv.indexOf(`--${k}`);
    return i > 0 ? process.argv[i + 1] : d;
  };
  (async () => {
    const browser = await launchBrowser();
    const page = await openBot(browser, {
      url: arg('url', 'http://localhost:8443'),
      room: arg('room', 'test'),
      name: arg('name', 'bot'),
      adapt: arg('adapt', '1'),
      run: arg('run', ''),
    });
    await new Promise((r) => setTimeout(r, Number(arg('secs', '60')) * 1000));
    await closeBot(page);
    await browser.close();
  })();
}
