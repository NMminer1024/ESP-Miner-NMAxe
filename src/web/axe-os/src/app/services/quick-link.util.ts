export type Sha256dCoin = 'BTC' | 'BCH' | 'DGB' | 'XEC';

interface QuickLinkRule {
  // substring matched against the stratum host (without port)
  match: string;
  // coin fixed by the pool itself; used as a hint when the address is ambiguous
  coin?: Sha256dCoin;
  build: (address: string, coin: Sha256dCoin) => string | undefined;
}

const MOLEPOOL_COINS: Sha256dCoin[] = ['BTC', 'BCH', 'XEC'];

const stripAddressPrefix = (address: string): string =>
  address.replace(/^(ecash|bitcoincash):/i, '');

const withEcashPrefix = (address: string): string =>
  /^ecash:/i.test(address) ? address : `ecash:${address}`;

// Sorted longest-match-first so e.g. au.solobtc.nmminer.com wins over solobtc.nmminer.com.
const RULES: QuickLinkRule[] = ([
  {
    match: 'public-pool.io',
    coin: 'BTC',
    build: (address) => `https://web.public-pool.io/#/app/${address}`
  },
  {
    match: 'ocean.xyz',
    coin: 'BTC',
    build: (address) => `https://ocean.xyz/stats/${address}`
  },
  {
    match: 'solo.ckpool.org',
    coin: 'BTC',
    build: (address) => `https://stats.ckpool.org/users/${address}`
  },
  {
    match: 'xec.nmminer.com',
    coin: 'XEC',
    // verified: workername must carry the ecash: prefix, otherwise the page 404s
    build: (address) => `https://xec.nmminer.com/user?workername=${withEcashPrefix(address)}`
  },
  {
    match: 'au.solobtc.nmminer.com',
    coin: 'BTC',
    build: (address) => `https://au.solobtc.nmminer.com/#/app/${address}`
  },
  {
    match: 'solobtc.nmminer.com',
    coin: 'BTC',
    build: (address) => `https://solobtc.nmminer.com/#/app/${address}`
  },
  {
    match: 'molepool.com',
    build: (address, coin) => MOLEPOOL_COINS.includes(coin)
      ? `https://${coin.toLowerCase()}.molepool.com/account/${stripAddressPrefix(address)}`
      : undefined
  },
  {
    match: 'dgb-stratum.solominer.net',
    coin: 'DGB',
    build: (address) => `https://digibyte.solominer.net/#/app/${address}`
  },
] as QuickLinkRule[]).sort((a, b) => b.match.length - a.match.length);

// A molepool stratum host like "bch.molepool.com" names the coin in its subdomain.
function molepoolSubdomainCoin(host: string): Sha256dCoin | undefined {
  const sub = host.split('.')[0]?.toUpperCase();
  return (['BTC', 'BCH', 'DGB', 'XEC'] as const).find(c => c === sub);
}

// Unambiguous address forms win; ambiguous legacy/cashaddr forms defer to the pool hint.
export function detectCoin(host: string, address: string, poolHint?: Sha256dCoin): Sha256dCoin {
  const lower = address.toLowerCase();
  if (lower.startsWith('ecash:')) return 'XEC';
  if (lower.startsWith('bitcoincash:')) return 'BCH';
  if (lower.startsWith('dgb1')) return 'DGB';
  if (/^D[1-9A-HJ-NP-Za-km-z]{25,34}$/.test(address)) return 'DGB';  // DGB base58 P2PKH
  if (/^S[1-9A-HJ-NP-Za-km-z]{25,34}$/.test(address)) return 'DGB';  // DGB base58 P2SH
  if (lower.startsWith('bc1')) return 'BTC';

  const hint = poolHint ?? molepoolSubdomainCoin(host);

  // bare cashaddr (q.../p..., 42 chars): shared by BCH and XEC
  if (/^[qp][a-z0-9]{41}$/.test(lower)) return hint === 'XEC' ? 'XEC' : (hint ?? 'BCH');
  // legacy base58 1.../3...: shared by BTC, BCH and XEC
  return hint ?? 'BTC';
}

export function resolveQuickLink(poolUrl: string, poolUser: string): string | undefined {
  const host = (poolUrl || '')
    .replace(/^stratum\+tcp:\/\//i, '')
    .replace(/^https?:\/\//i, '')
    .split(':')[0]
    .split('/')[0]
    .toLowerCase();
  const address = (poolUser || '').split('.')[0];
  if (!host || !address) return undefined;

  const rule = RULES.find(r => host.includes(r.match));
  if (!rule) return undefined;

  const coin = detectCoin(host, address, rule.coin);
  return rule.build(address, coin);
}
