var binding = require('../build/Release/vock');

var rate = 24000,
    a = new binding.Audio(rate, rate / 100, rate / 250);

a.ondata = function(data) {
  a.enqueue(0, data);
};
a.start();
