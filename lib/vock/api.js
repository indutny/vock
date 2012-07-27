var msgpack = require('msgpack-js');

var api = exports;

//
// ### function Api (instance, server)
// #### @instance {Vock.Instance} instance
// #### @server {Object} server address
// API methods wrapper
//
function Api(instance, server) {
  this.server = server;
  this.socket = instance.socket;
  this.seq = 0;
  this.timeout = 5000;
  this.watchInterval = 2000;
};

//
// ### function create (instance, server)
// #### @instance {Vock.Instance} instance
// #### @server {Object} server address
// Constructor wrapper
//
api.create = function create(instance, server) {
  return new Api(instance, server);
};

//
// ### function query (data, errback, callback)
// #### @data {Object} packet
// #### @errback {Function} callback for errors
// #### @callback {Function} regular callback
// Internal method for querying server
//
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

//
// ### function create (callback)
// #### @callback {Function} callback
// Send create request to server
//
Api.prototype.create = function create(callback) {
  this.query({ type: 'create' }, callback, function(packet) {
    callback(null, packet.id);
  });
};

//
// ### function connect (id, callback)
// #### @id {String} room id
// #### @callback {Function} callback
// Connects to existing room (or creates new)
//
Api.prototype.connect = function connect(id, callback) {
  var self = this;

  this.query({ type: 'connect', id: id }, callback, function(packet) {
    if (packet.members.length !== 0) return callback(null, packet.members);
    self.watch(id, callback);
  });
};

//
// ### function watch (id, callback)
// #### @id {String} room id
// #### @callback {Function} callback
// Wait for one room member to appear
//
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
