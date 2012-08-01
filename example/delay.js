var binding = require('../build/Release/vock');

var rate = 8000,
    a = new binding.Audio(rate, rate / 100);

a.ondata = function(data) {
  setTimeout(function() {
    a.enqueue(data);
  }, 2000);
};
a.start();
