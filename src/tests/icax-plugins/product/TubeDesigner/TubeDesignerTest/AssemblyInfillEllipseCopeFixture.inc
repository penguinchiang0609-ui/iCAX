// Exact 15-node infill/post cope subgraph from the U-shaped staircase regression.
R"neutralmodel({
  "schema": "icax.neutral-model",
  "schemaVersion": 1,
  "template": {
    "id": "test.actual-elliptical-post-infill-cope",
    "version": "1.0.0"
  },
  "coordinateSystem": "right-handed-x-width-y-depth-z-height",
  "lengthUnit": "mm",
  "parameters": {},
  "geometry": [
    {
      "key": "shared.tube.profile.eae060a75db642d22be7bb97074d8b4bb82df6b592258b59466112a7e2135cce",
      "operator": "profile2d",
      "inputs": [],
      "arguments": {
        "placement": {
          "origin": [
            0,
            0,
            0
          ],
          "xAxis": [
            1,
            0,
            0
          ],
          "yAxis": [
            0,
            1,
            0
          ]
        },
        "contours": [
          {
            "kind": "path",
            "closed": true,
            "segments": [
              {
                "kind": "line",
                "start": [
                  -10,
                  9
                ],
                "end": [
                  -10,
                  -9
                ]
              },
              {
                "kind": "arc",
                "start": [
                  -10,
                  -9
                ],
                "middle": [
                  -9.707106781186548,
                  -9.707106781186548
                ],
                "end": [
                  -9,
                  -10
                ]
              },
              {
                "kind": "line",
                "start": [
                  -9,
                  -10
                ],
                "end": [
                  9,
                  -10
                ]
              },
              {
                "kind": "arc",
                "start": [
                  9,
                  -10
                ],
                "middle": [
                  9.707106781186548,
                  -9.707106781186548
                ],
                "end": [
                  10,
                  -9
                ]
              },
              {
                "kind": "line",
                "start": [
                  10,
                  -9
                ],
                "end": [
                  10,
                  9
                ]
              },
              {
                "kind": "arc",
                "start": [
                  10,
                  9
                ],
                "middle": [
                  9.707106781186548,
                  9.707106781186548
                ],
                "end": [
                  9,
                  10
                ]
              },
              {
                "kind": "line",
                "start": [
                  9,
                  10
                ],
                "end": [
                  -9,
                  10
                ]
              },
              {
                "kind": "arc",
                "start": [
                  -9,
                  10
                ],
                "middle": [
                  -9.707106781186548,
                  9.707106781186548
                ],
                "end": [
                  -10,
                  9
                ]
              }
            ]
          },
          {
            "kind": "path",
            "closed": true,
            "segments": [
              {
                "kind": "line",
                "start": [
                  -8.5,
                  7.5
                ],
                "end": [
                  -8.5,
                  -7.5
                ]
              },
              {
                "kind": "arc",
                "start": [
                  -8.5,
                  -7.5
                ],
                "middle": [
                  -8.207106781186548,
                  -8.207106781186548
                ],
                "end": [
                  -7.5,
                  -8.5
                ]
              },
              {
                "kind": "line",
                "start": [
                  -7.5,
                  -8.5
                ],
                "end": [
                  7.5,
                  -8.5
                ]
              },
              {
                "kind": "arc",
                "start": [
                  7.5,
                  -8.5
                ],
                "middle": [
                  8.207106781186548,
                  -8.207106781186548
                ],
                "end": [
                  8.5,
                  -7.5
                ]
              },
              {
                "kind": "line",
                "start": [
                  8.5,
                  -7.5
                ],
                "end": [
                  8.5,
                  7.5
                ]
              },
              {
                "kind": "arc",
                "start": [
                  8.5,
                  7.5
                ],
                "middle": [
                  8.207106781186548,
                  8.207106781186548
                ],
                "end": [
                  7.5,
                  8.5
                ]
              },
              {
                "kind": "line",
                "start": [
                  7.5,
                  8.5
                ],
                "end": [
                  -7.5,
                  8.5
                ]
              },
              {
                "kind": "arc",
                "start": [
                  -7.5,
                  8.5
                ],
                "middle": [
                  -8.207106781186548,
                  8.207106781186548
                ],
                "end": [
                  -8.5,
                  7.5
                ]
              }
            ]
          }
        ]
      }
    },
    {
      "key": "shared.tube.extrude.279654437fb890f97e50fcda2539a66602d9f3391ce055d63cb9db46381d696d",
      "operator": "extrude",
      "inputs": [
        "shared.tube.profile.eae060a75db642d22be7bb97074d8b4bb82df6b592258b59466112a7e2135cce"
      ],
      "arguments": {
        "vector": [
          0,
          1.1368683772161603e-13,
          681.105957574728
        ]
      }
    },
    {
      "key": "flight.1.guard.1.infill.0.1.solid",
      "operator": "transform",
      "inputs": [
        "shared.tube.extrude.279654437fb890f97e50fcda2539a66602d9f3391ce055d63cb9db46381d696d"
      ],
      "arguments": {
        "placement": {
          "origin": [
            110,
            565,
            799.4871794871794
          ],
          "xAxis": [
            0,
            1,
            0
          ],
          "yAxis": [
            -0.5692099788303083,
            0,
            0.8221921916437787
          ],
          "zAxis": [
            0.8221921916437787,
            0,
            0.5692099788303083
          ]
        }
      }
    },
    {
      "key": "process.bd1e0647598d0795904a863f.0",
      "operator": "profile2d",
      "inputs": [],
      "arguments": {
        "placement": {
          "origin": [
            0,
            0,
            0
          ],
          "xAxis": [
            1,
            0,
            0
          ],
          "yAxis": [
            0,
            1,
            0
          ]
        },
        "contours": [
          {
            "kind": "path",
            "closed": true,
            "segments": [
              {
                "kind": "line",
                "start": [
                  130,
                  465
                ],
                "end": [
                  650,
                  465
                ]
              },
              {
                "kind": "line",
                "start": [
                  650,
                  465
                ],
                "end": [
                  650,
                  665
                ]
              },
              {
                "kind": "line",
                "start": [
                  650,
                  665
                ],
                "end": [
                  130,
                  665
                ]
              },
              {
                "kind": "line",
                "start": [
                  130,
                  665
                ],
                "end": [
                  130,
                  465
                ]
              }
            ]
          }
        ]
      }
    },
    {
      "key": "process.bd1e0647598d0795904a863f.1",
      "operator": "extrude",
      "inputs": [
        "process.bd1e0647598d0795904a863f.0"
      ],
      "arguments": {
        "vector": [
          0,
          0,
          1590
        ]
      }
    },
    {
      "key": "process.bd1e0647598d0795904a863f.operation.0",
      "operator": "boolean",
      "inputs": [
        "flight.1.guard.1.infill.0.1.solid",
        "process.bd1e0647598d0795904a863f.1"
      ],
      "arguments": {
        "operation": "intersect",
        "target": "flight.1.guard.1.infill.0.1.solid",
        "tools": [
          "process.bd1e0647598d0795904a863f.1"
        ]
      }
    },
    {
      "key": "process.83fa65dc210548e1aeb74668.0",
      "operator": "profile2d",
      "inputs": [],
      "arguments": {
        "placement": {
          "origin": [
            0,
            0,
            0
          ],
          "xAxis": [
            1,
            0,
            0
          ],
          "yAxis": [
            0,
            1,
            0
          ]
        },
        "contours": [
          {
            "kind": "path",
            "closed": true,
            "segments": [
              {
                "kind": "ellipseArc",
                "center": [
                  0,
                  0
                ],
                "majorRadius": 50,
                "minorRadius": 10,
                "rotation": 0,
                "startAngle": 0,
                "endAngle": 1.5707963267948966
              },
              {
                "kind": "ellipseArc",
                "center": [
                  0,
                  0
                ],
                "majorRadius": 50,
                "minorRadius": 10,
                "rotation": 0,
                "startAngle": 1.5707963267948966,
                "endAngle": 3.141592653589793
              },
              {
                "kind": "ellipseArc",
                "center": [
                  0,
                  0
                ],
                "majorRadius": 50,
                "minorRadius": 10,
                "rotation": 0,
                "startAngle": 3.141592653589793,
                "endAngle": 4.71238898038469
              },
              {
                "kind": "ellipseArc",
                "center": [
                  0,
                  0
                ],
                "majorRadius": 50,
                "minorRadius": 10,
                "rotation": 0,
                "startAngle": 4.71238898038469,
                "endAngle": 6.283185307179586
              }
            ]
          }
        ]
      }
    },
    {
      "key": "process.83fa65dc210548e1aeb74668.1",
      "operator": "extrude",
      "inputs": [
        "process.83fa65dc210548e1aeb74668.0"
      ],
      "arguments": {
        "vector": [
          0,
          0,
          1039.2307692307693
        ]
      }
    },
    {
      "key": "process.83fa65dc210548e1aeb74668.2",
      "operator": "transform",
      "inputs": [
        "process.83fa65dc210548e1aeb74668.1"
      ],
      "arguments": {
        "placement": {
          "origin": [
            130,
            565,
            160
          ],
          "xAxis": [
            1,
            0,
            0
          ],
          "yAxis": [
            0,
            1,
            0
          ],
          "zAxis": [
            0,
            0,
            1
          ]
        }
      }
    },
    {
      "key": "process.83fa65dc210548e1aeb74668.3",
      "operator": "profile2d",
      "inputs": [],
      "arguments": {
        "placement": {
          "origin": [
            0,
            0,
            0
          ],
          "xAxis": [
            1,
            0,
            0
          ],
          "yAxis": [
            0,
            1,
            0
          ]
        },
        "contours": [
          {
            "kind": "path",
            "closed": true,
            "segments": [
              {
                "kind": "ellipseArc",
                "center": [
                  0,
                  0
                ],
                "majorRadius": 50,
                "minorRadius": 10,
                "rotation": 0,
                "startAngle": 0,
                "endAngle": 1.5707963267948966
              },
              {
                "kind": "ellipseArc",
                "center": [
                  0,
                  0
                ],
                "majorRadius": 50,
                "minorRadius": 10,
                "rotation": 0,
                "startAngle": 1.5707963267948966,
                "endAngle": 3.141592653589793
              },
              {
                "kind": "ellipseArc",
                "center": [
                  0,
                  0
                ],
                "majorRadius": 50,
                "minorRadius": 10,
                "rotation": 0,
                "startAngle": 3.141592653589793,
                "endAngle": 4.71238898038469
              },
              {
                "kind": "ellipseArc",
                "center": [
                  0,
                  0
                ],
                "majorRadius": 50,
                "minorRadius": 10,
                "rotation": 0,
                "startAngle": 4.71238898038469,
                "endAngle": 6.283185307179586
              }
            ]
          }
        ]
      }
    },
    {
      "key": "process.83fa65dc210548e1aeb74668.4",
      "operator": "extrude",
      "inputs": [
        "process.83fa65dc210548e1aeb74668.3"
      ],
      "arguments": {
        "vector": [
          0,
          0,
          1039.2307692307693
        ]
      }
    },
    {
      "key": "process.83fa65dc210548e1aeb74668.5",
      "operator": "transform",
      "inputs": [
        "process.83fa65dc210548e1aeb74668.4"
      ],
      "arguments": {
        "placement": {
          "origin": [
            650,
            565,
            520
          ],
          "xAxis": [
            1,
            0,
            0
          ],
          "yAxis": [
            0,
            1,
            0
          ],
          "zAxis": [
            0,
            0,
            1
          ]
        }
      }
    },
    {
      "key": "process.83fa65dc210548e1aeb74668.6",
      "operator": "boolean",
      "inputs": [
        "process.83fa65dc210548e1aeb74668.2",
        "process.83fa65dc210548e1aeb74668.5"
      ],
      "arguments": {
        "operation": "union"
      }
    },
    {
      "key": "process.83fa65dc210548e1aeb74668.operation.0",
      "operator": "boolean",
      "inputs": [
        "process.bd1e0647598d0795904a863f.operation.0",
        "process.83fa65dc210548e1aeb74668.6"
      ],
      "arguments": {
        "operation": "subtract",
        "target": "process.bd1e0647598d0795904a863f.operation.0",
        "tools": [
          "process.83fa65dc210548e1aeb74668.6"
        ]
      }
    },
    {
      "key": "flight.1.guard.1.infill.0.1.placed",
      "operator": "transform",
      "inputs": [
        "process.83fa65dc210548e1aeb74668.operation.0"
      ],
      "arguments": {
        "placement": {
          "origin": [
            0,
            0,
            0
          ],
          "xAxis": [
            1,
            0,
            0
          ],
          "yAxis": [
            0,
            1,
            0
          ],
          "zAxis": [
            0,
            0,
            1
          ]
        }
      }
    }
  ],
  "items": [],
  "outputs": [],
  "relationships": [],
  "tables": [],
  "diagnostics": [],
  "extensions": {}
})neutralmodel"
