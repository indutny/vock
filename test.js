var vock = require('./build/Release/vock'),
    Buffer = require('buffer').Buffer;

var audio = new vock.Audio(48000, 960),
    buffers = [];

audio.ondata = function(buffer) {
};

audio.oninputready = function() {
  console.log('input ready ' + +new Date());
};

audio.onoutputready = function() {
  console.log('output ready ' + +new Date());
};

audio.start();
console.log('start');
