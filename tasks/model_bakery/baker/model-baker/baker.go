package modelbaker

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strings"
	"unsafe"

	"github.com/qmuntal/gltf"
	"github.com/qmuntal/gltf/modeler"
)

type Baker struct {
}

func NewBaker() *Baker {
	return &Baker{}
}

func (b *Baker) BakeScene(scenepath string) error {
	doc, err := b.load(scenepath)
	if err != nil {
		return fmt.Errorf("load: %w", err)
	}

	resultMeshes := b.processMeshes(doc)
	b.updateExtensions(doc)

	dir := filepath.Dir(scenepath)
	base := strings.TrimSuffix(filepath.Base(scenepath), filepath.Ext(scenepath))
	modelPath := filepath.Join(dir, base)
	b.updateBuffer(modelPath, doc, &resultMeshes)
	b.updateBufferView(doc, &resultMeshes)

	var newAccessors []*gltf.Accessor
	for i := range doc.Meshes {
		mesh := doc.Meshes[i]
		for j := range mesh.Primitives {
			relem := &resultMeshes.Relems[resultMeshes.Meshes[i].FirstRelem+uint32(j)]
			b.updateAccessors(&newAccessors, mesh.Primitives[j], relem)
		}
	}
	doc.Accessors = newAccessors

	outputPath := modelPath + "_baked.gltf"
	if err := gltf.Save(doc, outputPath); err != nil {
		return err
	}

	return nil
}

func (Baker) load(scenepath string) (*gltf.Document, error) {
	scenepath, err := filepath.Abs(scenepath)
	if err != nil {
		return nil, fmt.Errorf("get abs path of %q: %w", scenepath, err)
	}

	return gltf.Open(scenepath)
}

func (Baker) updateExtensions(doc *gltf.Document) {
	doc.ExtensionsRequired = append(doc.ExtensionsRequired, "KHR_mesh_quantization")
	doc.ExtensionsUsed = append(doc.ExtensionsUsed, "KHR_mesh_quantization")
}

func (b *Baker) processMeshes(doc *gltf.Document) ProcessedMeshes {
	var result ProcessedMeshes

	warns := []error{}
	for _, mesh := range doc.Meshes {
		meshInfo := Mesh{
			FirstRelem: uint32(len(result.Relems)),
			RelemCount: uint32(len(mesh.Primitives)),
		}

		for _, prim := range mesh.Primitives {
			if prim.Mode != gltf.PrimitiveTriangles {
				warns = append(warns, fmt.Errorf("non-triangles primitive: %v", prim))
				meshInfo.RelemCount--
				continue
			}

			posIdx, hasPosition := prim.Attributes[gltf.POSITION]
			normalIdx, hasNormals := prim.Attributes[gltf.NORMAL]
			tangentIdx, hasTangents := prim.Attributes[gltf.TANGENT]
			texcoordIdx, hasTexcoord := prim.Attributes["TEXCOORD_0"]

			if !hasPosition || prim.Indices == nil {
				warns = append(warns, fmt.Errorf("missing required POSITION or indices primitive: %v", prim))
				meshInfo.RelemCount--
				continue
			}

			indexAccessor := doc.Accessors[*prim.Indices]
			posAccessor := doc.Accessors[posIdx]

			relem := Element{
				VertexOffset: uint32(len(result.Vertices)),
				IndexOffset:  uint32(len(result.Indices)),
				IndexCount:   uint32(indexAccessor.Count),
			}

			vertexCount := posAccessor.Count

			positions := make([][3]float32, vertexCount)
			modeler.ReadPosition(doc, doc.Accessors[posIdx], positions)

			var minPos, maxPos [3]float32
			if len(positions) > 0 {
				minPos = positions[0]
				maxPos = positions[0]
				for _, pos := range positions {
					for i := range 3 {
						if pos[i] < minPos[i] {
							minPos[i] = pos[i]
						}
						if pos[i] > maxPos[i] {
							maxPos[i] = pos[i]
						}
					}
				}
			}

			relem.MinPos = minPos
			relem.MaxPos = maxPos

			var normals [][3]float32
			if hasNormals {
				normals = make([][3]float32, vertexCount)
				modeler.ReadNormal(doc, doc.Accessors[normalIdx], normals)
			}

			var tangents [][4]float32
			if hasTangents {
				tangents = make([][4]float32, vertexCount)
				modeler.ReadTangent(doc, doc.Accessors[tangentIdx], tangents)
			}

			var texcoords [][2]float32
			if hasTexcoord {
				texcoords = make([][2]float32, vertexCount)
				modeler.ReadTextureCoord(doc, doc.Accessors[texcoordIdx], texcoords)
			}

			for i := 0; i < int(vertexCount); i++ {
				var vtx Vertex
				pos := positions[i]

				var normal [3]float32
				var tangent [3]float32
				var texcoord [2]float32

				if hasNormals {
					normal = normals[i]
				}
				if hasTangents {
					tangent = [3]float32{tangents[i][0], tangents[i][1], tangents[i][2]}
				}
				if hasTexcoord {
					texcoord = texcoords[i]
				}

				encodedNormal := encodeNormal(NewVec4(normal[:]...))
				encodedTangent := encodeNormal(NewVec4(tangent[:]...))

				vtx.PositionAndNormal = NewVec4(pos[0], pos[1], pos[2], uint32ToFloat32(encodedNormal))
				vtx.TexCoordAndTangentAndPadding = NewVec4(texcoord[0], texcoord[1], uint32ToFloat32(encodedTangent))

				result.Vertices = append(result.Vertices, vtx)
			}

			indices := make([]uint32, indexAccessor.Count)
			modeler.ReadIndices(doc, indexAccessor, indices)
			result.Indices = append(result.Indices, indices...)

			result.Relems = append(result.Relems, relem)
		}

		result.Meshes = append(result.Meshes, meshInfo)
	}

	warn := errors.Join(warns...)
	if warn != nil {
		fmt.Fprintln(os.Stderr, warn)
	}

	return result
}

func (b *Baker) updateBuffer(binPath string, doc *gltf.Document, resMeshes *ProcessedMeshes) {
	data := bytes.NewBuffer(nil)

	for _, idx := range resMeshes.Indices {
		binary.Write(data, binary.LittleEndian, idx)
	}

	for _, vtx := range resMeshes.Vertices {
		binary.Write(data, binary.LittleEndian, vtx.PositionAndNormal)
		binary.Write(data, binary.LittleEndian, vtx.TexCoordAndTangentAndPadding)
	}

	doc.Buffers = []*gltf.Buffer{{
		Name:       filepath.Base(strings.TrimSuffix(binPath, filepath.Ext(binPath))),
		URI:        filepath.Base(binPath) + "_baked.bin",
		ByteLength: data.Len(),
		Data:       data.Bytes(),
	}}
}

func (b *Baker) updateBufferView(doc *gltf.Document, resMeshes *ProcessedMeshes) {
	indsSize := len(resMeshes.Indices) * 4
	vertsSize := len(resMeshes.Vertices) * int(unsafe.Sizeof(Vertex{}))

	indsBufferView := &gltf.BufferView{
		Buffer:     0,
		ByteLength: indsSize,
		Target:     gltf.TargetElementArrayBuffer,
	}

	vertsBufferView := &gltf.BufferView{
		Buffer:     0,
		ByteOffset: indsSize,
		ByteLength: vertsSize,
		ByteStride: int(unsafe.Sizeof(Vertex{})),
		Target:     gltf.TargetArrayBuffer,
	}

	doc.BufferViews = []*gltf.BufferView{indsBufferView, vertsBufferView}
}

func (b *Baker) updateAccessors(newAccessors *[]*gltf.Accessor, prim *gltf.Primitive, relem *Element) {
	_, hasNormals := prim.Attributes[gltf.NORMAL]
	_, hasTangents := prim.Attributes[gltf.TANGENT]
	_, hasTexcoord := prim.Attributes["TEXCOORD_0"]

	prim.Attributes = make(gltf.PrimitiveAttributes)

	indexOffset := int(relem.IndexOffset)
	count := int(relem.IndexCount)
	offset := int(relem.VertexOffset) * int(unsafe.Sizeof(Vertex{}))

	indicesAccessor := &gltf.Accessor{
		BufferView:    gltf.Index(0),
		ByteOffset:    indexOffset * 4,
		ComponentType: gltf.ComponentUint,
		Count:         count,
		Type:          gltf.AccessorScalar,
	}
	prim.Indices = gltf.Index(len(*newAccessors))
	*newAccessors = append(*newAccessors, indicesAccessor)

	posAccessor := &gltf.Accessor{
		BufferView:    gltf.Index(1),
		ByteOffset:    offset,
		ComponentType: gltf.ComponentFloat,
		Count:         count,
		Type:          gltf.AccessorVec3,
		Min:           []float64{float64(relem.MinPos[0]), float64(relem.MinPos[1]), float64(relem.MinPos[2])},
		Max:           []float64{float64(relem.MaxPos[0]), float64(relem.MaxPos[1]), float64(relem.MaxPos[2])},
	}
	prim.Attributes[gltf.POSITION] = len(*newAccessors)
	*newAccessors = append(*newAccessors, posAccessor)

	if hasNormals {
		normalAccessor := &gltf.Accessor{
			BufferView:    gltf.Index(1),
			ByteOffset:    offset + 12,
			Normalized:    true,
			ComponentType: gltf.ComponentByte,
			Count:         count,
			Type:          gltf.AccessorVec3,
		}
		prim.Attributes[gltf.NORMAL] = len(*newAccessors)
		*newAccessors = append(*newAccessors, normalAccessor)
	}

	if hasTexcoord {
		texcoordAccessor := &gltf.Accessor{
			BufferView:    gltf.Index(1),
			ByteOffset:    offset + 16,
			ComponentType: gltf.ComponentFloat,
			Count:         count,
			Type:          gltf.AccessorVec2,
		}
		prim.Attributes["TEXCOORD_0"] = len(*newAccessors)
		*newAccessors = append(*newAccessors, texcoordAccessor)
	}

	if hasTangents {
		tangentAccessor := &gltf.Accessor{
			BufferView:    gltf.Index(1),
			ByteOffset:    offset + 24,
			Normalized:    true,
			ComponentType: gltf.ComponentByte,
			Count:         count,
			Type:          gltf.AccessorVec4,
		}
		prim.Attributes[gltf.TANGENT] = len(*newAccessors)
		*newAccessors = append(*newAccessors, tangentAccessor)
	}
}

type Element struct {
	VertexOffset uint32
	IndexOffset  uint32
	IndexCount   uint32
	MinPos       [3]float32
	MaxPos       [3]float32
}

type Mesh struct {
	FirstRelem uint32
	RelemCount uint32
}

type Vec4 struct {
	X, Y, Z, W float32
}

func (v Vec4) Arr() [4]float32 {
	return [4]float32{
		v.X, v.Y, v.Z, v.W,
	}
}

func NewVec4(coords ...float32) (res Vec4) {
	switch len(coords) {
	case 4:
		res.W = coords[3]
		fallthrough
	case 3:
		res.Z = coords[2]
		fallthrough
	case 2:
		res.Y = coords[1]
		fallthrough
	case 1:
		res.X = coords[0]
		fallthrough
	case 0:
		return
	default:
		panic(fmt.Sprintf("len(coords:%v) > 4", coords))
	}
}

type Vertex struct {
	PositionAndNormal            Vec4
	TexCoordAndTangentAndPadding Vec4
}

type ProcessedMeshes struct {
	Vertices []Vertex
	Indices  []uint32
	Relems   []Element
	Meshes   []Mesh
}

func encodeNormal(normal Vec4) uint32 {
	// May be bug
	bytes := [4]uint8{
		uint8(math.Round(float64(normal.X * 127.0))),
		uint8(math.Round(float64(normal.Y * 127.0))),
		uint8(math.Round(float64(normal.Z * 127.0))),
		uint8(math.Round(float64(normal.W * 127.0))),
	}

	return binary.LittleEndian.Uint32(bytes[:])
}

func uint32ToFloat32(u uint32) float32 {
	return *(*float32)(unsafe.Pointer(&u))
}

func calculateMin(positions [][3]float32) []float32 {
	if len(positions) == 0 {
		return []float32{0, 0, 0}
	}
	min := positions[0]
	for _, pos := range positions[1:] {
		for i := 0; i < 3; i++ {
			if pos[i] < min[i] {
				min[i] = pos[i]
			}
		}
	}
	return min[:]
}

func calculateMax(positions [][3]float32) []float32 {
	if len(positions) == 0 {
		return []float32{0, 0, 0}
	}
	max := positions[0]
	for _, pos := range positions[1:] {
		for i := 0; i < 3; i++ {
			if pos[i] > max[i] {
				max[i] = pos[i]
			}
		}
	}
	return max[:]
}
