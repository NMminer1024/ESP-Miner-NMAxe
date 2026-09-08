import {detectCoin, resolveQuickLink} from './quick-link.util';

describe('quick-link.util', () => {

  describe('detectCoin', () => {
    it('detects XEC from ecash: prefix', () => {
      expect(detectCoin('', 'ecash:qp09xfp0ktcrcyysxvrtyf6dp9fgpgrll5yckwk9hn')).toBe('XEC');
    });
    it('detects BCH from bitcoincash: prefix', () => {
      expect(detectCoin('', 'bitcoincash:qp07l6vwr0qpsdvk93wy2v8z4s4lpfnsmywr9pn3p2')).toBe('BCH');
    });
    it('detects DGB from dgb1 / D / S addresses', () => {
      expect(detectCoin('', 'dgb1qxyz')).toBe('DGB');
      expect(detectCoin('', 'DTestAddressForDigibyte1234567')).toBe('DGB');
      expect(detectCoin('', 'STestAddressForDigibyte1234567')).toBe('DGB');
    });
    it('detects BTC from bc1', () => {
      expect(detectCoin('', 'bc1qe7r70mv34xhjpwyanvpa43an95rha3287yrv77')).toBe('BTC');
    });
    it('legacy base58 defaults to BTC without hint', () => {
      expect(detectCoin('', '1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa')).toBe('BTC');
    });
    it('legacy base58 defers to pool hint', () => {
      expect(detectCoin('xec.nmminer.com', '129zaNdkZtBYPXJgT2TUwX5c7e46HJbi8Y', 'XEC')).toBe('XEC');
    });
    it('bare cashaddr defers to pool hint, defaults to BCH', () => {
      expect(detectCoin('xec.nmminer.com', 'qp09xfp0ktcrcyysxvrtyf6dp9fgpgrll5yckwk9hn', 'XEC')).toBe('XEC');
      expect(detectCoin('', 'qp09xfp0ktcrcyysxvrtyf6dp9fgpgrll5yckwk9hn')).toBe('BCH');
    });
  });

  describe('resolveQuickLink', () => {
    const BTC_ADDR = '1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa';

    it('public-pool', () => {
      expect(resolveQuickLink('public-pool.io', `${BTC_ADDR}.worker1`))
        .toBe(`https://web.public-pool.io/#/app/${BTC_ADDR}`);
    });
    it('ocean', () => {
      expect(resolveQuickLink('ocean.xyz', `${BTC_ADDR}.w`))
        .toBe(`https://ocean.xyz/stats/${BTC_ADDR}`);
    });
    it('ckpool matches host regardless of port', () => {
      expect(resolveQuickLink('solo.ckpool.org:4334', `${BTC_ADDR}.w`))
        .toBe(`https://stats.ckpool.org/users/${BTC_ADDR}`);
    });
    it('xec.nmminer.com adds ecash: prefix when missing', () => {
      expect(resolveQuickLink('xec.nmminer.com', 'qp09xfp0ktcrcyysxvrtyf6dp9fgpgrll5yckwk9hn.w'))
        .toBe('https://xec.nmminer.com/user?workername=ecash:qp09xfp0ktcrcyysxvrtyf6dp9fgpgrll5yckwk9hn');
      expect(resolveQuickLink('xec.nmminer.com', 'ecash:qp09xfp0ktcrcyysxvrtyf6dp9fgpgrll5yckwk9hn.w'))
        .toBe('https://xec.nmminer.com/user?workername=ecash:qp09xfp0ktcrcyysxvrtyf6dp9fgpgrll5yckwk9hn');
    });
    it('nmminer solobtc: regional host wins over base host', () => {
      expect(resolveQuickLink('au.solobtc.nmminer.com', `${BTC_ADDR}.w`))
        .toBe(`https://au.solobtc.nmminer.com/#/app/${BTC_ADDR}`);
      expect(resolveQuickLink('solobtc.nmminer.com', `${BTC_ADDR}.w`))
        .toBe(`https://solobtc.nmminer.com/#/app/${BTC_ADDR}`);
    });
    it('molepool resolves coin from stratum subdomain and strips address prefix', () => {
      expect(resolveQuickLink('bch.molepool.com:3333', 'bitcoincash:qp07l6vwr0qpsdvk93wy2v8z4s4lpfnsmywr9pn3p2.w'))
        .toBe('https://bch.molepool.com/account/qp07l6vwr0qpsdvk93wy2v8z4s4lpfnsmywr9pn3p2');
      expect(resolveQuickLink('molepool.com', 'ecash:qp09xfp0ktcrcyysxvrtyf6dp9fgpgrll5yckwk9hn.w'))
        .toBe('https://xec.molepool.com/account/qp09xfp0ktcrcyysxvrtyf6dp9fgpgrll5yckwk9hn');
      expect(resolveQuickLink('btc.molepool.com', `${BTC_ADDR}.w`))
        .toBe(`https://btc.molepool.com/account/${BTC_ADDR}`);
    });
    it('molepool has no DGB pool', () => {
      expect(resolveQuickLink('dgb.molepool.com', 'DTestAddressForDigibyte1234567.w')).toBeUndefined();
    });
    it('solominer digibyte', () => {
      expect(resolveQuickLink('dgb-stratum.solominer.net', 'DTestAddressForDigibyte1234567.w'))
        .toBe('https://digibyte.solominer.net/#/app/DTestAddressForDigibyte1234567');
    });
    it('returns undefined for unknown pools or empty input', () => {
      expect(resolveQuickLink('unknown.pool.io', `${BTC_ADDR}.w`)).toBeUndefined();
      expect(resolveQuickLink('', BTC_ADDR)).toBeUndefined();
      expect(resolveQuickLink('ocean.xyz', '')).toBeUndefined();
    });
  });
});
