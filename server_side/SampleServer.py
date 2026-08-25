from flask import Flask, send_file

app = Flask(__name__)

@app.route("/image")
def image():
    return send_file("./test.bmp", mimetype="application/octet-stream")

@app.route("/cover_image")
def cover_image():
    return send_file("./cover_image.bmp", mimetype="application/octet-stream")

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=80)
