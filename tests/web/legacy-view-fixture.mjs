// Test-only projection for constructing genuine V4–V12 byte fixtures from a
// current V15 export. Those formats had twelve info IDs, so changing only the
// version marker would create an invalid old payload when factory Names=12.
// Unknown/newer values are not silently repaired; only the introduced Names
// default maps to the corresponding historical line's meaning.
export function legacyViewInfo(payload) {
  const copy = payload.slice();
  const first = 2217 + 1 + 12;
  for (let view = 0; view < 9; ++view) {
    for (let field = 0; field < 10; ++field) {
      if (field === 2 || field === 3) continue;
      const offset = first + view * 10 + field;
      if (offset >= copy.length || copy[offset] !== 12) continue;
      copy[offset] = field === 0 ? 1 : field === 4 ? 4 : 3;
    }
  }
  return copy;
}
