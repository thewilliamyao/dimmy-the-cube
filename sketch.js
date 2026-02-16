const serial = new p5.WebSerial();
let brightestSide = -1;
let floorSide = -1;
let portButton;

let myFont;

function preload() {
  // Load a default font (or use a .ttf file)
  myFont = loadFont('https://cdnjs.cloudflare.com/ajax/libs/topcoat/0.8.0/font/SourceCodePro-Bold.otf');
}

function setup() {
  createCanvas(600, 600, WEBGL);
  textFont(myFont);
  
  if (!navigator.serial) {
    alert("WebSerial not supported. Use Chrome or Edge.");
  }
  
  serial.on("noport", makePortButton);
  serial.on("portavailable", openPort);
  serial.on("data", serialEvent);
  
  serial.getPorts();
}

function draw() {
  background(30);
  orbitControl();
  
  ambientLight(100);
  pointLight(255, 255, 255, 0, 0, 200);
  
  drawCube(150);
  
  // Display debug info
  push();
  fill(255);
  translate(-250, -250, 0);
  text("Floor: " + floorSide + " | Brightest: " + brightestSide, 0, 0);
  pop();
}

function serialEvent() {
  let line = serial.readStringUntil('\n');
  
  if (line) {
    line = trim(line);
    
    // Skip "READY" message
    if (line === "READY") {
      console.log("Arduino ready, starting handshake...");
      serial.write("x"); // Send anything to complete handshake
      return;
    }
    
    console.log("Data: " + line);
    
    let parts = line.split(',');
    if (parts.length >= 3) {
      floorSide = int(parts[0]);
      brightestSide = int(parts[1]);
    }
  }
}

function makePortButton() {
  portButton = createButton('Connect Arduino');
  portButton.position(10, 10);
  portButton.mousePressed(() => serial.requestPort());
}

function openPort() {
  serial.open({ baudRate: 115200 }).then(() => {
    console.log("Port opened!");
  });
  if (portButton) portButton.hide();
}

function drawCube(s) {
  drawFace(0, 0, 0, s/2, 0, 0, s);
  drawFace(1, s/2, 0, 0, 0, HALF_PI, s);
  drawFace(2, 0, s/2, 0, HALF_PI, 0, s);
  drawFace(3, -s/2, 0, 0, 0, -HALF_PI, s);
  drawFace(4, 0, -s/2, 0, -HALF_PI, 0, s);
  drawFace(5, 0, 0, -s/2, 0, PI, s);
}

function drawFace(id, tx, ty, tz, rx, ry, s) {
  push();
  translate(tx, ty, tz);
  rotateX(rx);
  rotateY(ry);
  
  if (id === brightestSide) {
    fill(255, 255, 0);
    emissiveMaterial(255, 255, 0);
  } else if (id === floorSide) {
    fill(100, 100, 255);
    emissiveMaterial(0);
  } else {
    fill(150);
    emissiveMaterial(0);
  }
  
  stroke(255);
  plane(s);
  
  // Display the face number
  fill(0);
  textAlign(CENTER, CENTER);
  textSize(48);
  text(id, 0, 0, s/2 + 1);
  
  pop();
}