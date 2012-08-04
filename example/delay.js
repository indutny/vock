var binding = require('../build/Release/vock');

var rate = 8000,
    a = new binding.Audio(rate, rate / 100);

a.ondata = function(data) {
  setTimeout(function() {
    a.enqueue(2, data);
  }, 4000);

  setTimeout(function() {
    a.enqueue(0, data);
  }, 3000);

  setTimeout(function() {
    a.enqueue(1, data);
  }, 2000);

  setTimeout(function() {
    a.enqueue(3, data);
  }, 1000);
};
a.start();
