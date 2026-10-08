// Impairment evaluation matrix. For every netem profile x {adaptation off,
// on}: apply the profile on lo, start a call between N headless bots, record
// stats for DURATION seconds, then clear the impairment.
//
// Run inside WSL/Linux (tc netem is Linux-only). tc needs root, so cache
// sudo first:
//   sudo -v && node bench/run-matrix.js
// Env: PROFILES="baseline loss5" DURATION=60 PEERS=2 ADAPT="0 1" PORT=8443

const { execFileSync, spawn } = require('child_process');
const path = require('path');
const { launchBrowser, openBot, closeBot } = require('./bot');

const ROOT = path.join(__dirname, '..');
const PORT = Number(process.env.PORT) || 8443;
const URL = `http://localhost:${PORT}`;
const DURATION = Number(process.env.DURATION) || 60;
const WARMUP = Number(process.env.WARMUP) || 8; // seconds of clean call before impairment
const PEERS = Math.min(Number(process.env.PEERS) || 2, 4);
const PROFILES = (process.env.PROFILES ||
  'baseline loss1 loss5 loss10 loss20 burst5 delay100 jitter30 jitter60 rate2m rate1m rate500k rate250k combo').split(/\s+/);
const ADAPT = (process.env.ADAPT || '0 1').split(/\s+/);
const DEV = process.env.DEV || 'lo';
const TAG = process.env.TAG || new Date().toISOString().slice(0, 16).replace(/[:T]/g, '');

const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));

function impair(profile) {
  execFileSync('sudo', ['-n', path.join(ROOT, 'scripts', 'impair.sh'), profile, DEV], { stdio: 'inherit' });
}

async function startServer() {
  const proc = spawn(process.execPath, [path.join(ROOT, 'server', 'index.js')], {
    env: { ...process.env, PORT: String(PORT) },
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  await new Promise((resolve, reject) => {
    proc.stdout.on('data', (d) => { if (d.toString().includes('Mini-Meet on')) resolve(); });
    proc.on('exit', (code) => reject(new Error(`server exited ${code}`)));
  });
  return proc;
}

(async () => {
  const server = await startServer();
  let browser = null;
  const cleanup = () => {
    try { impair('clear'); } catch {}
    server.kill();
  };
  process.on('SIGINT', () => { cleanup(); process.exit(1); });

  try {
    browser = await launchBrowser();
    for (const profile of PROFILES) {
      for (const adapt of ADAPT) {
        const run = `${TAG}_${profile}_adapt${adapt}_p${PEERS}`;
        const room = `bench-${profile}-${adapt}`.slice(0, 32);
        console.log(`\n=== ${run}`);
        impair('clear');
        const pages = [];
        for (let i = 0; i < PEERS; i++) {
          pages.push(await openBot(browser, { url: URL, room, name: `bot${'ABCD'[i]}`, adapt, run }));
          await sleep(1);
        }
        await sleep(WARMUP);
        impair(profile);
        await sleep(DURATION);
        for (const p of pages) await closeBot(p);
        impair('clear');
        await sleep(2);
      }
    }
  } finally {
    await browser?.close();
    cleanup();
  }
  console.log(`\nDone. CSVs in results/ (prefix ${TAG}). Next: python3 analysis/plot.py results/${TAG}_*.csv`);
})().catch((err) => {
  console.error(err);
  try { impair('clear'); } catch {}
  process.exit(1);
});
