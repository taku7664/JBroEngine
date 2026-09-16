namespace JBro.Launcher.Model;

/// <summary>프로젝트 파일이 어느 프레임워크 위에서 도는지다. 엔진의 FrameworkKind 와 같은 값이다.</summary>
public enum FrameworkKind
{
    Framework2D,
    Framework3D,
}

/// <summary>
/// `.jproject` 에서 런처가 쓰는 값만 뽑아 담는다. 엔진이 읽는 키가 더 많지만 런처가
/// 알아야 하는 것은 목록에 보여 줄 것과 어느 엔진으로 열지뿐이다.
/// </summary>
public sealed class ProjectFileInfo
{
    /// <summary>이 프로젝트를 여는 엔진 버전이다(`EngineVersion`). 없으면 프로젝트가 아니다.</summary>
    public required string EngineVersion { get; init; }

    /// <summary>2D 인지 3D 인지다(`Framework`). 없으면 프로젝트가 아니다.</summary>
    public required FrameworkKind Framework { get; init; }

    /// <summary>`Build.ProductName` 이다. 비어 있으면 파일 이름을 쓴다.</summary>
    public string ProductName { get; init; } = string.Empty;

    public int ResolutionWidth { get; init; }

    public int ResolutionHeight { get; init; }
}
