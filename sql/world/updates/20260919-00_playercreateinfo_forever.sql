-- AscEmu - WoW Forever 1.60.1.69913
-- Extend playercreateinfo build range and add Forever race/class entries.

SET NAMES utf8mb4;

ALTER TABLE `playercreateinfo`
    MODIFY COLUMN `build` INT UNSIGNED NOT NULL DEFAULT 18414;

INSERT INTO `playercreateinfo`
    (`build`, `race`, `class`, `mapID`, `zoneID`, `positionX`, `positionY`, `positionZ`, `orientation`)
VALUES
    -- Race 95: Warrior, Hunter, Rogue, Mage, Druid
    (69913, 95,  1, 0, 9, -8914.57, -133.909, 80.5378, 5.13806),
    (69913, 95,  3, 0, 9, -8914.57, -133.909, 80.5378, 5.13806),
    (69913, 95,  4, 0, 9, -8914.57, -133.909, 80.5378, 5.13806),
    (69913, 95,  8, 0, 9, -8914.57, -133.909, 80.5378, 5.13806),
    (69913, 95, 11, 0, 9, -8914.57, -133.909, 80.5378, 5.13806),

    -- Race 96: Warrior, Hunter, Rogue, Shaman, Druid
    (69913, 96,  1, 0, 9, -8914.57, -133.909, 80.5378, 5.13806),
    (69913, 96,  3, 0, 9, -8914.57, -133.909, 80.5378, 5.13806),
    (69913, 96,  4, 0, 9, -8914.57, -133.909, 80.5378, 5.13806),
    (69913, 96,  7, 0, 9, -8914.57, -133.909, 80.5378, 5.13806),
    (69913, 96, 11, 0, 9, -8914.57, -133.909, 80.5378, 5.13806);
