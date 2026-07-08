const SubresourceIntegrityPlugin = require('webpack-subresource-integrity');

module.exports = {
  output: {
    crossOriginLoading: 'anonymous',
    filename: '[name].[hash:4].js',
    chunkFilename: '[name].[hash:4].js'
  },
  plugins: [
    new SubresourceIntegrityPlugin({
      hashFuncNames: ['sha256', 'sha384']
    })
  ]
};