package main

import "flag"
import "log"
import "net/http"
import "time"

import _ "embed"

//go:embed out.json
var content []byte

//go:embed out-sjcprod.json
var contentSjcProd []byte

func main() {
  var delayMs int
  var sjcProd bool
  flag.IntVar(&delayMs, "delayms", 0, "Delay responses by this time (in milliseconds)")
  flag.BoolVar(&sjcProd, "sjcprod", false, "Point client to sjc05 production ingest, with no video encodings")
  flag.Parse()

  http.HandleFunc("/", func(res http.ResponseWriter, req *http.Request) {
    time.Sleep(time.Millisecond * time.Duration(delayMs))
    if sjcProd {
        res.Write(contentSjcProd)
    } else {
        res.Write(content)
    }
  })
  
  log.Fatal(http.ListenAndServe("127.0.0.1:8787", nil))
}


