const assert = require('node:assert/strict');
const {Worker, isMainThread, parentPort} = require('node:worker_threads');
const fs = require('node:fs/promises');
const crypto = require('node:crypto');
const {square} = require('./helper.js');
if (!isMainThread) {
    parentPort.postMessage(Array.from({length:1000}, (_, i) => square(i)).reduce((a,b)=>a+b,0));
} else {
    (async () => {
        const sum = await new Promise((resolve,reject)=>{
            const worker = new Worker(__filename);
            worker.on('message',resolve);worker.on('error',reject);
            worker.on('exit',code=>{if(code)reject(new Error(`worker exit ${code}`));});
        });
        assert.equal(sum,332833500);
        const name='مرحبا';
        await fs.writeFile('js-result.json',JSON.stringify({name,sum}), 'utf8');
        assert.deepEqual(JSON.parse(await fs.readFile('js-result.json','utf8')),{name,sum});
        assert.equal(crypto.createHash('sha256').update(name).digest('hex').length,64);
        await new Promise(resolve=>setTimeout(resolve,5));
        console.log(`JS_OK:${sum}`);
    })().catch(error=>{console.error(error);process.exitCode=1;});
}
