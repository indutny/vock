var vock = require('./build/Release/vock'),
    fs = require('fs'),
    Buffer = require('buffer').Buffer;

var audio = new vock.Audio(),
    buffers = [];

audio.ondata = function(buffer) {
  audio.enqueue(buffer);
};

console.log('start');
audio.start();
