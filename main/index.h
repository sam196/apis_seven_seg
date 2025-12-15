const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP8266 Display Controller</title>
    <style>
        body {
            background-color: #111;
            color: white;
            font-family: Arial, sans-serif;
            text-align: center;
            margin: 0;
            padding: 0;
        }
        h1 {
            margin-top: 30px;
            color: #00ffcc;
        }
        input[type="text"] {
            width: 80%;
            padding: 10px;
            margin: 10px 0;
            border-radius: 8px;
            border: none;
            text-align: center;
            font-size: 18px;
        }
        button {
            padding: 10px 20px;
            font-size: 16px;
            margin: 10px;
            border: none;
            border-radius: 8px;
            background-color: #00ffcc;
            color: #111;
            cursor: pointer;
        }
        button:hover {
            background-color: #00ffaa;
        }
    </style>
</head>
<body>
    <h1>SAMTRONICS DISPLAY CONTROL</h1>
    <input type="text" id="message" placeholder="Enter text or number">
    <br>
    <button onclick="sendData()">Send to Display</button>

    <script>
        function sendData() {
            const msg = document.getElementById('message').value;
            fetch('/send?data=' + encodeURIComponent(msg))
            .then(response => response.text())
            .then(data => alert(data))
            .catch(err => console.error(err));
        }
    </script>
</body>
</html>
)rawliteral";
