var binding = require('./build/Release/vock');

var a = new binding.Audio(8000, 960);
a.ondata = function(data) {
  setTimeout(function() {
    a.enqueue(data);
  }, 1000);
};
a.oninputready = function() {};
a.onoutputready = function() {};
a.start();
