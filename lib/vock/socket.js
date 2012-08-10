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

  var self = this;

  this.socket = dgram.createSocket('udp4');
  this.socket.bind(0);
  this.server = options.server;
  this.relayVersion = [0, 1];
  this.relaySeq = 0;

  this.port = this.socket.address().port;
  this.natTtl = 60 * 30;

  // Create port-forward mapping if possible
  this.pmpClient = pmp.connect(netroute.getGateway());
  this.pmpClient.portMapping({
    private: this.port,
    public: this.port,
    type: 'udp',
    ttl: this.natTtl
  }, function(err) {
    if (err) return;

    self.emit('nat:traversal', 'pmp', self.port);
  });

  this.upnpClient = natUpnp.createClient();
  this.upnpClient.portMapping({
    public: this.port,
    private: this.port,
    protocol: 'udp',
    description: 'Vock - VoIP on node.js',
    ttl: this.natTtl
  }, function(err) {
    if (err) return;

    self.emit('nat:traversal', 'upnp', self.port);
  });

  this.socket.on('message', this.ondata.bind(this));
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
// ### function send (packet, target)
// #### @packet {Object} packet
// #### @target {Object} target address
// Sends encoded packet to target
//
Socket.prototype.send = function(packet, target) {
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

  // Unwrap relay packets from server
  if (msg.protocol === 'relay' &&
      (addr.address == this.server.address ||
       this.server.address == '0.0.0.0') &&
      addr.port == this.server.port) {
    addr = msg.from;
    msg = msg.body;
  }

  this.emit('data', msg, addr);
};
