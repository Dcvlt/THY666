// macOS/Linux commonly provide python3; Windows installations provide python.
import { spawnSync } from 'node:child_process';
const executable = process.platform === 'win32' ? 'python' : 'python3';
const result = spawnSync(executable, process.argv.slice(2), { stdio: 'inherit' });
if (result.error) {
  console.error(`Could not start ${executable}: ${result.error.message}`);
}
process.exit(result.status ?? 1);
