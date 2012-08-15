var util = require('util'),
    dgram = require('dgram'),
    net = require('net'),
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

  this.tsocket = null;
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
// ### function bind (port, callback)
// #### @ports {Array} Port numbers
// #### @callback {Function} Continuation to proceed to
Socket.prototype.bind = function bind(ports, callback) {
  var self = this,
      port = ports[0] || 0;

  if (this.tsocket) {
    this.tsocket.removeAllListeners('data');
    this.tsocket.removeAllListeners('error');
    this.tsocket.removeAllListeners('listening');
  }

  // Bind TCP socket
  this.tsocket = net.createServer();
  this.tsocket.on('error', function(err) {
    // Try to bind to a random port
    self.bind(ports.slice(1), callback);
  });
  this.tsocket.listen(port, function() {
    self.tsocket.removeAllListeners('error');
    self.port = self.tsocket.address().port;

    // Bind UDP socket
    if (self.socket) {
      self.socket.removeAllListeners('message');
      self.socket.removeAllListeners('listening');
    }

    self.socket = dgram.createSocket('udp4');
    self.socket.on('message', self.ondata.bind(self));
    self.socket.once('listening', function() {
      callback(self.port, port !== 0);
    });

    try {
      self.socket.bind(self.port);
    } catch (e) {
      self.bind(ports.slice(1), callback);
    }
  });
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
    list = list || [];

    self.bind(list.map(function(item) {
      return item.private.port;
    }), function(port, reuse) {
      // Unwind accumulated callbacks
      var queue = self._initQueue;
      self._initialized = true;
      self._initQueue = [];
      queue.forEach(function(callback) {
        callback.call(self);
      });

      // Do not add mappings if we've found existing one
      if (reuse) {
        self.emit('nat:traversal:udp', 'upnp', port);
        self.emit('nat:traversal:tcp', 'upnp', port);
        return;
      }

      // Create port-forward mapping if possible
      var protocols = ['udp', 'tcp'];
      protocols.forEach(function(protocol) {
        self.upnpClient.portMapping({
          public: port,
          private: port,
          protocol: protocol,
          description: 'Vock - VoIP on node.js',
          ttl: 0 // Unlimited, since most routers doesn't support other value
        }, function(err) {
          if (err) return;
          self.emit('nat:traversal:' + protocol, 'upnp', port);
        });

        self.pmpClient.portMapping({
          private: port,
          public: port,
          type: protocol,
          ttl: self.natTtl
        }, function(err) {
          if (err) return;

          self.emit('nat:traversal:' + protocol, 'pmp', port);
        });
      });
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
