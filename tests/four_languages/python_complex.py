import asyncio
import concurrent.futures
import csv
import hashlib
import json
import multiprocessing
import pathlib
import sqlite3
import ssl
from helper import square

async def work():
    async def task(n):
        await asyncio.sleep(0.005)
        return square(n)
    return sum(await asyncio.gather(*(task(n) for n in range(100))))

if __name__ == '__main__':
    assert asyncio.run(work()) == 328350
    with concurrent.futures.ProcessPoolExecutor(max_workers=2, mp_context=multiprocessing.get_context('spawn')) as pool:
        assert list(pool.map(square, range(10))) == [n*n for n in range(10)]
    db = sqlite3.connect(':memory:')
    db.execute('create table items (n integer)')
    db.executemany('insert into items values (?)', ((n,) for n in range(1000)))
    assert db.execute('select sum(n) from items').fetchone()[0] == 499500
    data = {'name': 'مرحبا', 'sum': 499500}
    path = pathlib.Path('python-result.json')
    path.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
    assert json.loads(path.read_text(encoding='utf-8')) == data
    assert len(hashlib.sha256(path.read_bytes()).hexdigest()) == 64
    print('PYTHON_OK:499500', flush=True)
