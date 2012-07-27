var msgpack = require('msgpack-js');

var api = exports;

function Api(instance, server) {
  this.server = server;
  this.socket = instance.socket;
  this.seq = 0;
  this.timeout = 5000;
  this.watchInterval = 2000;
};

api.create = function create(instance, server) {
  return new Api(instance, server);
};

Api.prototype.query = function(data, errback, callback) {
  var self = this,
      timeout,
      seq = data.seq = this.seq++;

  data.protocol = 'api';

  function onreply(message, addr) {
    if (message.protocol !== 'api' || message.seq !== seq) return;

    clearTimeout(timeout);
    self.socket.removeListener('data', onreply);

    if (message.type === 'error') {
      return errback(new Error(message.reason));
    }

    callback(message, addr);
  }
  this.socket.on('data', onreply);
  this.socket.send(data, this.server);

  timeout = setTimeout(function() {
    self.socket.removeListener('data', onreply);

    // Try again
    self.query(data, errback, callback);
  }, this.timeout);
};

Api.prototype.create = function create(callback) {
  this.query({ type: 'create' }, callback, function(packet) {
    callback(null, packet.id);
  });
};

Api.prototype.connect = function connect(id, callback) {
  this.query({ type: 'connect', id: id }, callback, function(packet) {
    callback(null, packet.members);
  });
};

Api.prototype.watch = function watch(id, callback) {
  var self = this;

  function get() {
    self.query({ type: 'info', id: id }, callback, function(packet) {
      if (packet.members.length === 0) {
        return setTimeout(get, self.watchInterval);
      }

      callback(null, packet.members);
    });
  }

  get();
};
