package modelbaker

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"sync"

	"github.com/karetskiiVO/slices"
	"github.com/qmuntal/gltf"
)

var i0 = sync.OnceValue(func() *int {
	return gltf.Index(0)
})
var i1 = sync.OnceValue(func() *int {
	return gltf.Index(1)
})

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

	dir := filepath.Dir(scenepath)
	base := strings.TrimSuffix(filepath.Base(scenepath), filepath.Ext(scenepath))
	modelPath := filepath.Join(dir, base)

	b.optimize(doc)

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

func (b Baker) optimize(doc *gltf.Document) {
	var result struct {
		vertices  []CompressedVertex
		indices   []uint32
		accessors []*gltf.Accessor
	}

	newAccessor := func() *gltf.Accessor {
		return new(gltf.Accessor)
	}
	floatUpscaler := func(x float32) float64 {
		return float64(x)
	}

	for _, mesh := range doc.Meshes {
		for _, prim := range mesh.Primitives {
			vertices, indices := b.acceptPrimitive(doc, prim)

			prim.Indices = gltf.Index(len(result.accessors))
			result.accessors = append(result.accessors, newAccessor())
			idxAccessor := result.accessors[len(result.accessors)-1]
			*idxAccessor = gltf.Accessor{
				BufferView:    i0(),
				ByteOffset:    binary.Size(result.indices),
				Count:         len(indices),
				ComponentType: gltf.ComponentUint,
				Type:          gltf.AccessorScalar,

				Min: []float64{float64(slices.Min(indices))},
				Max: []float64{float64(slices.Max(indices))},
			}

			prim.Attributes["POSITION"] = len(result.accessors)
			result.accessors = append(result.accessors, newAccessor())
			posAccessor := result.accessors[len(result.accessors)-1]
			*posAccessor = gltf.Accessor{
				BufferView:    i1(),
				ByteOffset:    binary.Size(result.vertices) + int(reflect.TypeFor[CompressedVertex]().Field(0).Offset),
				Count:         len(vertices),
				ComponentType: gltf.ComponentFloat,
				Type:          gltf.AccessorVec3,

				Min: slices.Map(
					slices.Reduce(
						vertices[0].Position, vertices,
						func(v Vec3f, elem Vertex) Vec3f { return *v.Min(v, elem.Position) },
					).Slice(),
					floatUpscaler,
				),
				Max: slices.Map(
					slices.Reduce(
						vertices[0].Position, vertices,
						func(v Vec3f, elem Vertex) Vec3f { return *v.Max(v, elem.Position) },
					).Slice(),
					floatUpscaler,
				),
			}

			prim.Attributes["NORMAL"] = len(result.accessors)
			result.accessors = append(result.accessors, newAccessor())
			normAccessor := result.accessors[len(result.accessors)-1]
			*normAccessor = gltf.Accessor{
				BufferView:    i1(),
				ByteOffset:    binary.Size(result.vertices) + int(reflect.TypeFor[CompressedVertex]().Field(1).Offset),
				Count:         len(vertices),
				ComponentType: gltf.ComponentByte,
				Type:          gltf.AccessorVec3,
				Normalized:    true,
			}

			prim.Attributes["TEXCOORD_0"] = len(result.accessors)
			result.accessors = append(result.accessors, newAccessor())
			texAccessor := result.accessors[len(result.accessors)-1]
			*texAccessor = gltf.Accessor{
				BufferView:    i1(),
				ByteOffset:    binary.Size(result.vertices) + int(reflect.TypeFor[CompressedVertex]().Field(3).Offset),
				Count:         len(vertices),
				ComponentType: gltf.ComponentFloat,
				Type:          gltf.AccessorVec2,
			}

			prim.Attributes["TANGENT"] = len(result.accessors)
			result.accessors = append(result.accessors, newAccessor())
			tangentAccessor := result.accessors[len(result.accessors)-1]
			*tangentAccessor = gltf.Accessor{
				BufferView:    i1(),
				ByteOffset:    binary.Size(result.vertices) + int(reflect.TypeFor[CompressedVertex]().Field(4).Offset),
				Count:         len(vertices),
				ComponentType: gltf.ComponentByte,
				Type:          gltf.AccessorVec4,
				Normalized:    true,
			}

			result.indices = append(result.indices, indices...)
			result.vertices = append(result.vertices, slices.Map(vertices, compress)...)
		}
	}

	buffer := bytes.NewBuffer(nil)
	indexView := &gltf.BufferView{
		Buffer:     0,
		ByteOffset: buffer.Len(),
		ByteLength: binary.Size(result.indices),
	}
	binary.Write(buffer, binary.LittleEndian, result.indices)

	vertexView := &gltf.BufferView{
		Buffer:     0,
		ByteOffset: buffer.Len(),
		ByteLength: binary.Size(result.vertices),
		ByteStride: int(reflect.TypeFor[CompressedVertex]().Size()),
	}
	binary.Write(buffer, binary.LittleEndian, result.vertices)

	doc.Accessors = result.accessors
	doc.Buffers = []*gltf.Buffer{{
		ByteLength: buffer.Len(),
		Data:       buffer.Bytes(),
	}}
	doc.BufferViews = []*gltf.BufferView{indexView, vertexView}
	doc.ExtensionsUsed = append(doc.ExtensionsUsed, "KHR_mesh_quantization")
	doc.ExtensionsRequired = append(doc.ExtensionsRequired, "KHR_mesh_quantization")
}

func (Baker) acceptPrimitive(doc *gltf.Document, prim *gltf.Primitive) (vertices []Vertex, indices []uint32) {
	const (
		INDICES  = 0
		POSITION = 1
		NORMAL   = 2
		TANGENT  = 3
		TEXCOORD = 4
	)

	if prim.Mode != gltf.PrimitiveTriangles {
		fmt.Fprintln(os.Stderr, "non triangle mode, skip")
		return []Vertex{}, []uint32{}
	}

	accessorIDs := []any{
		*prim.Indices,
		"POSITION",
		"NORMAL",
		"TANGENT",
		"TEXCOORD_0",
	}

	type Pipeline struct {
		accessor *gltf.Accessor
		bufView  *gltf.BufferView
		r        io.Reader
		stride   int
	}

	assesorIdx := slices.Map(accessorIDs, func(v any) int {
		switch vt := v.(type) {
		case int:
			return vt
		case string:
			res, ok := prim.Attributes[vt]
			if !ok {
				res = -1
			}
			return res
		}
		return -1
	})

	pipeline := slices.Map(assesorIdx, func(idx int) Pipeline {
		if idx == -1 {
			return Pipeline{
				r: emptyReader{},
			}
		}

		accessor := doc.Accessors[idx]
		bufView := doc.BufferViews[*accessor.BufferView]
		r := bytes.NewBuffer(doc.Buffers[bufView.Buffer].Data[bufView.ByteOffset+accessor.ByteOffset:])
		stride := bufView.ByteStride
		if stride == 0 {
			stride = gltf.SizeOfElement(accessor.ComponentType, accessor.Type)
		}

		return Pipeline{
			accessor: accessor,
			bufView:  bufView,
			r:        r,
			stride:   stride,
		}
	})

	remover := [128]byte{}
	vertices = make([]Vertex, 0, pipeline[POSITION].accessor.Count)
	for range pipeline[POSITION].accessor.Count {
		v := Vertex{
			Position: NewVec3[float32](),
			Normal:   NewVec3[float32](0, 1, 0),
			Tangent:  NewVec3[float32](1, 0, 0),
			TexCoord: NewVec2[float32](),
		}

		slices.Map(
			slices.Join(
				[]int{POSITION, NORMAL, TANGENT, TEXCOORD},
				[]any{&v.Position, &v.Normal, &v.Tangent, &v.TexCoord},
			),
			func(pair slices.Pair[int, any]) struct{} {
				binary.Read(pipeline[pair.First].r, binary.LittleEndian, pair.Second)

				overhead := max(0, pipeline[pair.First].stride-int(reflect.TypeOf(pair.Second).Elem().Size()))
				pipeline[pair.First].r.Read(remover[:overhead])
				return struct{}{}
			},
		)

		vertices = append(vertices, v)
	}

	switch pipeline[INDICES].accessor.ComponentType {
	case gltf.ComponentUshort:
		indices = make([]uint32, 0, pipeline[INDICES].accessor.Count)
		for range pipeline[INDICES].accessor.Count {
			var buf uint16
			binary.Read(pipeline[INDICES].r, binary.LittleEndian, &buf)
			indices = append(indices, uint32(buf))
		}
	case gltf.ComponentUint:
		indices = make([]uint32, pipeline[INDICES].accessor.Count)
		binary.Read(pipeline[INDICES].r, binary.LittleEndian, &indices)
	}
	return vertices, indices
}

func compress(v Vertex) CompressedVertex {
	compressfloat := func(v float32) int8 {
		if v < -1 || 1 < v {
			panic(fmt.Sprintf("overflow of float: %v", v))
		}

		return int8(v * 127)
	}

	return CompressedVertex{
		Position: v.Position,
		Norm:     NewVec3(slices.Map(v.Normal.Slice(), compressfloat)...),
		TexCoord: v.TexCoord,
		Tangent:  NewVec4(append(slices.Map(v.Tangent.Slice(), compressfloat), 127)...),
	}
}
