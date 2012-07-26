var util = require('util'),
    dgram = require('dgram'),
    msgpack = require('msgpack-js'),
    EventEmitter = require('events').EventEmitter;

var socket = exports;

function Socket() {
  EventEmitter.call(this);
  this.socket = dgram.createSocket('udp4');
  this.socket.bind(0);

  this.socket.on('message', this.ondata.bind(this));
};
util.inherits(Socket, EventEmitter);

socket.create = function create() {
  return new Socket();
};

Socket.prototype.send = function(packet, target) {
  try {
    var raw = msgpack.encode(packet);
  } catch (e) {
    this.emit('error', e);
    return;
  }

  this.socket.send(raw, 0, raw.length, target.port, target.host);
};

Socket.prototype.ondata = function ondata(raw, addr) {
  try {
    var msg = msgpack.decode(raw);
  } catch (e) {
    this.emit('error', e);
    return;
  }

  this.emit('data', msg, addr);
};
