var util = require('util'),
    dgram = require('dgram'),
    msgpack = require('msgpack-js'),
    EventEmitter = require('events').EventEmitter;

var socket = exports;

function Socket(options) {
  EventEmitter.call(this);
  this.socket = dgram.createSocket('udp4');
  this.socket.bind(0);
  this.server = options.server;
  this.relayVersion = [0, 1];
  this.relaySeq = 0;

  this.socket.on('message', this.ondata.bind(this));
};
util.inherits(Socket, EventEmitter);

socket.create = function create(options) {
  return new Socket(options);
};

Socket.prototype.send = function(packet, target) {
  try {
    var raw = msgpack.encode(packet);
  } catch (e) {
    this.emit('error', e);
    return;
  }

  this.socket.send(raw, 0, raw.length, target.port, target.address);
};

Socket.prototype.relay = function relay(packet, target) {
  var wrapper = {
    protocol: 'relay',
    seq: this.relaySeq++,
    id: target.id,
    to: { address: target.address, port: target.port },
    body: packet,
    version: this.relayVersion
  };

  this.send(wrapper, this.server);
};

Socket.prototype.ondata = function ondata(raw, addr) {
  try {
    var msg = msgpack.decode(raw);
  } catch (e) {
    this.emit('error', e);
    return;
  }

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
