var util = require('util'),
    dgram = require('dgram'),
    msgpack = require('msgpack-js'),
    pmp = require('nat-pmp'),
    natUpnp = require('nat-upnp'),
    netroute = require('netroute'),
    EventEmitter = require('events').EventEmitter;

var socket = exports;

//
// ### function Socket (options)
// #### @options {Object} Socket options
// msgpack encoder/decoder wrapper over udp socket
//
function Socket(options) {
  EventEmitter.call(this);

  this.socket = null;
  this.port = null;

  this.server = options.server;
  this.relayVersion = [0, 1];
  this.relaySeq = 0;
  this._initQueue = [];
  this._initialized = false;

  this.natTtl = 60 * 30;

  this.init();
};
util.inherits(Socket, EventEmitter);

//
// ### function create (options)
// #### @options {Object} Socket options
// Constructor wrapper
//
socket.create = function create(options) {
  return new Socket(options);
};

//
// ### function init ()
// Internal
//
Socket.prototype.init = function init() {
  var self = this;

  this.upnpClient = natUpnp.createClient();
  this.pmpClient = pmp.connect(netroute.getGateway());

  this.upnpClient.getMappings({
    local: true,
    description: /vock/i
  }, function(err, list) {
    var port = 0;

    // Try to reuse port
    if (!err && list.length > 0) {
      port = list[0].private.port;
      self.emit('nat:traversal', 'upnp', port);
    }

    self.socket = dgram.createSocket('udp4');
    self.socket.on('message', self.ondata.bind(self));
    self.socket.bind(port);
    self.port = self.socket.address().port;

    // Unwind accumulated callbacks
    var queue = self._initQueue;
    self._initialized = true;
    self._initQueue = [];
    queue.forEach(function(callback) {
      callback.call(self);
    });

    // Do not add mappings if we've found existing one
    if (port != 0) return;

    // Create port-forward mapping if possible
    self.upnpClient.portMapping({
      public: self.port,
      private: self.port,
      protocol: 'udp',
      description: 'Vock - VoIP on node.js',
      ttl: self.natTtl
    }, function(err) {
      if (err) return;

      self.emit('nat:traversal', 'upnp', self.port);
    });

    self.pmpClient.portMapping({
      private: self.port,
      public: self.port,
      type: 'udp',
      ttl: self.natTtl
    }, function(err) {
      if (err) return;

      self.emit('nat:traversal', 'pmp', self.port);
    });
  });
};

//
// ### function send (packet, target)
// #### @packet {Object} packet
// #### @target {Object} target address
// Sends encoded packet to target
//
Socket.prototype.send = function(packet, target) {
  if (!this._initialized) {
    this._initQueue.push(function() {
      this.send(packet, target);
    });
    return;
  }

  try {
    var raw = msgpack.encode(packet);
  } catch (e) {
    this.emit('error', e);
    return;
  }

  this.socket.send(raw, 0, raw.length, target.port, target.address);
};

//
// ### function relay (packet, id, target)
// #### @packet {Object} packet
// #### @id {String} room id
// #### @target {Object} target address
// Sends encoded packet to target through relay
//
Socket.prototype.relay = function relay(packet, id, target) {
  var wrapper = {
    protocol: 'relay',
    seq: this.relaySeq++,
    id: id,
    to: { address: target.address, port: target.port },
    body: packet,
    version: this.relayVersion
  };

  this.send(wrapper, this.server);
};

//
// ### function ondata (raw, addr)
// #### @raw {Buffer} packet
// #### @addr {Object} from address
// Receives incoming relay or direct packet
//
Socket.prototype.ondata = function ondata(raw, addr) {
  try {
    var msg = msgpack.decode(raw);
  } catch (e) {
    this.emit('error', e);
    return;
  }

  if (!msg) return this.emit('error', 'Empty message!');

  addr.relay = false;

  // Unwrap relay packets from server
  if (msg.protocol === 'relay' &&
      (addr.address == this.server.address ||
       this.server.address == '0.0.0.0') &&
      addr.port == this.server.port) {
    addr = msg.from;
    addr.relay = true;
    msg = msg.body;
  }

  this.emit('data', msg, addr);
};
