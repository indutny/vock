# Vock protocol

## Framing

All frames are JSON packed with [msgpack module](https://github.com/pgriess/node-msgpack).

```javascript
{
  "type": "hello" // frame type
  "version": [0, 1], // protocol version
  "seq": 1,       // sequence number
  ...             // other packet data
}
```

## Client <-> Client

Frame types:

* helo
* acpt
* voic
* ping
* clse


```
    Client1      Client2
hello        ->
            <-    hello
            <-    accept
accept       ->
voice        ->
            <-    voice
ping         ->
            <-    ping
clse         ->
            <-    close
```


## Client <-> Server

TODO (May be just STUN?)
