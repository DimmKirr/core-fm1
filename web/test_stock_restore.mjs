// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Optional checks against the user's original vendor file, never redistributed.
import assert from "node:assert/strict";
import {existsSync, readFileSync, writeFileSync} from "node:fs";
import {Package, buildStockRestorePackage, validateStockPackage} from "./fm1pkg.js";

const stockPath = new URL("../FM-1_v15.fwsc", import.meta.url);
const donorPath = new URL("../build/melodee.fwsc", import.meta.url);
if (!existsSync(stockPath) || !existsSync(donorPath)) {
  console.log("stock restore package: skipped (needs FM-1_v15.fwsc and build/melodee.fwsc)");
} else {
  const stock = new Uint8Array(readFileSync(stockPath)), donor = new Uint8Array(readFileSync(donorPath));
  const original = new Package(stock), source = new Package(donor);
  const restored = await buildStockRestorePackage(stock, donor, source.product);
  const result = new Package(restored);
  assert.equal(result.product, "FM-1_015");
  assert.deepEqual(result.flash, original.flash);
  const loader = pkg => {
    const entry = pkg.entries.find(e => e.type === 100);
    return pkg.raw.slice(entry.dataOff + pkg.skew, entry.dataOff + pkg.skew + entry.size);
  };
  assert.deepEqual(loader(result), loader(source));
  assert.notDeepEqual(loader(result), loader(original));
  const modified = stock.slice(); modified[50000] ^= 1;
  await assert.rejects(buildStockRestorePackage(modified, donor, source.product), /unmodified official/);
  await assert.rejects(buildStockRestorePackage(stock, stock, original.product), /restore loader/);
  await assert.rejects(buildStockRestorePackage(stock, donor, "FM-1_99999"), /restore loader/);
  const damaged = donor.slice(); damaged[damaged.length - 100] ^= 1;
  await assert.rejects(buildStockRestorePackage(stock, damaged, source.product), /restore loader/);
  // The transient browser package remains distinct from an unmodified vendor file.
  await assert.rejects(validateStockPackage(restored), /unmodified official/);
  writeFileSync(new URL("../build/stock-restore.fwsc", import.meta.url), restored);
  console.log("stock restore package: exact stock flash, exact Melodee loader, identity and invalid-input checks passed");
}
