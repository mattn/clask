window.addEventListener('DOMContentLoaded', function() {
 fetch('/api').then(function(resp) {
    return resp.json();
  }).then(function(json) {
    document.querySelector('#time').textContent = json.time;
  }); 
}, false)
