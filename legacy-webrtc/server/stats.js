// Appends call-quality samples posted by clients to per-run CSV files in
// results/. One row = one second of one inbound or outbound media stream.

const fs = require('fs');
const path = require('path');

const RESULTS_DIR = path.join(__dirname, '..', 'results');

const COLUMNS = [
  'ts', 'run', 'peer', 'remote', 'kind', 'direction',
  'bitrate_kbps', 'packets_lost', 'loss_pct', 'jitter_ms', 'rtt_ms',
  'fps', 'width', 'height', 'avail_out_kbps', 'limit_reason',
  'concealed_pct', 'freeze_count', 'adapt_on', 'adapt_level', 'candidate_type',
];

const SAFE_RUN = /^[A-Za-z0-9_.-]{1,80}$/;

function csvValue(v) {
  if (v === undefined || v === null) return '';
  const s = String(v);
  return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
}

function append(run, rows) {
  if (!SAFE_RUN.test(run) || !Array.isArray(rows)) return false;
  fs.mkdirSync(RESULTS_DIR, { recursive: true });
  const file = path.join(RESULTS_DIR, `${run}.csv`);
  const fresh = !fs.existsSync(file);
  const lines = rows.slice(0, 500).map((r) => COLUMNS.map((c) => csvValue(r[c])).join(','));
  fs.appendFileSync(file, (fresh ? COLUMNS.join(',') + '\n' : '') + lines.join('\n') + '\n');
  return true;
}

module.exports = { append, COLUMNS };
