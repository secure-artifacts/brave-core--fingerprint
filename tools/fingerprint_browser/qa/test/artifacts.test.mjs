import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import {unsignedMachOSha256} from '../lib/artifacts.mjs'

function fakeSignedMachO(signature) {
  const header = Buffer.alloc(32)
  header.writeUInt32LE(0xfeedfacf, 0)
  header.writeUInt32LE(2, 16)
  header.writeUInt32LE(88, 20)

  const linkedit = Buffer.alloc(72)
  linkedit.writeUInt32LE(0x19, 0)
  linkedit.writeUInt32LE(linkedit.length, 4)
  linkedit.write('__LINKEDIT', 8, 'ascii')
  linkedit.writeBigUInt64LE(BigInt(4096 + signature.length), 32)
  linkedit.writeBigUInt64LE(BigInt(7 + signature.length), 48)

  const command = Buffer.alloc(16)
  command.writeUInt32LE(0x1d, 0)
  command.writeUInt32LE(command.length, 4)
  command.writeUInt32LE(header.length + linkedit.length + command.length + 7, 8)
  command.writeUInt32LE(signature.length, 12)

  return Buffer.concat([header, linkedit, command, Buffer.from('payload'), signature])
}

test('unsignedMachOSha256 ignores ad-hoc signature payload and size', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-artifact-test-'))
  try {
    const first = path.join(directory, 'first.dylib')
    const second = path.join(directory, 'second.dylib')
    await fs.writeFile(first, fakeSignedMachO(Buffer.from('signature-one')))
    await fs.writeFile(second, fakeSignedMachO(Buffer.from('different-signature-two')))

    assert.equal(await unsignedMachOSha256(first), await unsignedMachOSha256(second))
  } finally {
    await fs.rm(directory, {recursive: true, force: true})
  }
})
