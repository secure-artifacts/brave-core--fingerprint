chrome.runtime.sendMessage({type: 'collect'}).then(result => {
  document.querySelector('#result').textContent = JSON.stringify(result, null, 2)
})
