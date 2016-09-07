import { Template } from 'meteor/templating';
import { ReactiveVar } from 'meteor/reactive-var';

import './main.html';

window.addEventListener('load', function() {
        window.doppler.init(function(bandwidth) {
          var date = new Date();
          getMovements(date.getTime(),bandwidth.left,bandwidth.right);
        });
});

var lastZero = 0;
var areaUnderCurve=0;
var togetherArea=0;
var movements = [];
var prevAway = 0;
var prevTowards=0;
var prevTogether=0;
var peakTowards = 0;
var peakAway = 0;


function getMovements(timestamp,away,towards){
  var threshold = 4;
  var awayUnit = 60;
  var towardsUnit=140;
  var togetherUnit = 250;
  
  //Moving Away
  if(away>threshold && towards<=threshold){
    //continuing away
    if(areaUnderCurve>=0)
      areaUnderCurve= areaUnderCurve+away;
    //switching from towards to away
    if(areaUnderCurve<0)
      areaUnderCurve = away;
    //One unit away
    if(areaUnderCurve>awayUnit){
      //console.log("away");
      areaUnderCurve=0;
      if(movements.length===0 || movements[movements.length-1]<0){
        movements.push(1);
        prevAway=timestamp;
        peakAway=0;
      }
      else if(timestamp - prevAway < 200)
        movements[movements.length-1]++;
      else
        movements.push(1);
      prevAway=timestamp;
      peakAway = Math.max(peakAway,away);
      togetherArea=0;
      //console.log(movements);
    }
  }
  else if(towards>threshold && away<=threshold){
    //continuing towards
    if(areaUnderCurve<=0)
      areaUnderCurve= areaUnderCurve-towards;
    //switching from away to towards
    if(areaUnderCurve>0)
      areaUnderCurve = -towards;
    //One unit towards
    if(areaUnderCurve<towardsUnit){
      //console.log("towards");
      areaUnderCurve=0;
      if(movements.length===0 || movements[movements.length-1]>0){
        movements.push(-1);
        prevTowards = timestamp;
        peakTowards=0;
      }
      else if(timestamp - prevTowards < 200)
        movements[movements.length-1]--;
      else
        movements.push(-1);
      prevTowards=timestamp;
      peakTowards=Math.max(peakTowards,towards);
      togetherArea=0;
      //console.log(movements);
    }
  }
  else if(towards>threshold &&away>threshold){
    togetherArea=togetherArea+away + towards;
    if(togetherArea>togetherUnit){
      prevTogether = timestamp;
      if(movements.length===0 || movements[movements.length-1]!==0){
        movements.push(0);
      }
      togetherArea=0;
    }
  }
  analyzeMovements(timestamp);
}

function analyzeMovements(timestamp){
  if(movements.length>1)
  {
    //console.log(movements);
    var length = movements.length;
    //single tap, double tap, and together
    if(movements[length-1] > 3 && movements[length-2] <8 && timestamp- prevTowards<800 && timestamp-prevAway>200 &&timestamp-prevTogether >500){
      console.log("tap");
      $("#box").css({"background-color": getRandomColor()});
      changeText("TAP");
      while(movements.length)
        movements.pop();
    }
    else if(movements[length-1] > 3 && movements[length-2] <8 && movements[length-3] > 3 && movements[length-4] <8)
    {
      console.log("double tap");
      var length = changeSize();

      $("#box").css({"width": length, "height":length});
      changeText("DOUBLE TAP!");
      while(movements.length)
        movements.pop();
    }
    else if(movements[length-1]===0){
      console.log("together");
      changeText("TOGETHER!");
      var length = $('#box').css('width');
      $("#box").fadeOut(300);
      $('#box').fadeIn(300);
      while(movements.length)
        movements.pop();
    }
  }
}

function getRandomColor() {
    var letters = '0123456789ABCDEF'.split('');
    var color = '#';
    for (var i = 0; i < 6; i++ ) {
        color += letters[Math.floor(Math.random() * 16)];
    }
    return color;

}

function changeSize(){
  var rand = Math.floor(Math.random()*300+200);
  return rand;
}

function changeText(string){
  var text = $("h1");
  text.html(string);
  text.fadeIn(50);
  text.fadeOut(1500);
}