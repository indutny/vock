var dns = require('dns');

var utils = exports;

utils.getAddress = function getAddress(addr, callback) {
  addr = addr.split(':', 2);

  var host = addr[0] || '0.0.0.0',
      port = addr[1] || 44123;

  dns.lookup(host, function(err, address) {
    if (err) return callback(err, host);

    callback(null, { address: address, port: port });
  });
};

function Logger() {
  this.queue = [];
  this.paused = false;
};

Logger.prototype.write = function write() {
  var args = Array.prototype.slice.call(arguments);
  if (this.paused) {
    this.queue.push(args);
    return;
  }

  console.log.apply(console, args);
};

Logger.prototype.pause = function pause() {
  this.paused = true;
};

Logger.prototype.resume = function resume() {
  this.queue.forEach(function(args) {
    console.log.apply(console, args);
  });

  this.paused = false;
  this.queue = [];
};

utils.createLogger = function createLogger() {
  return new Logger();
};
